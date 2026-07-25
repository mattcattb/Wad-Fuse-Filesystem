#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Wad.h"

namespace {

constexpr std::size_t kHeaderSize = 28;
constexpr std::size_t kMaxMetadataSize = 16 * 1024;
constexpr uint16_t kVersion = 1;
constexpr uint16_t kSuccess = 100;
constexpr uint16_t kError = 101;

enum Command : uint16_t {
  PING = 1,
  CREATE_ARTIFACT = 2,
  INSPECT = 3,
  TREE = 4,
  LIST = 5,
  STAT = 6,
  READ = 7,
  READ_RANGE = 8,
  MKDIR = 9,
  PUT = 10,
  RESET = 11,
  DOWNLOAD = 12,
  DELETE_ARTIFACT = 13,
};

struct Request {
  uint16_t type;
  uint64_t id;
  std::vector<std::string> fields;
  std::vector<char> body;
};

std::mutex locksMutex;
std::unordered_map<std::string, std::shared_ptr<std::shared_mutex>> artifactLocks;

std::string jsonEscape(const std::string &value) {
  std::string result;
  for (const unsigned char character : value) {
    switch (character) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (character < 0x20) {
          const char hex[] = "0123456789abcdef";
          result += "\\u00";
          result += hex[(character >> 4) & 0x0f];
          result += hex[character & 0x0f];
        } else {
          result += static_cast<char>(character);
        }
    }
  }
  return result;
}

std::vector<std::string> splitFields(const std::string &metadata) {
  std::vector<std::string> fields;
  std::stringstream stream(metadata);
  std::string field;
  while (std::getline(stream, field, '\t')) fields.push_back(field);
  if (!metadata.empty() && metadata.back() == '\t') fields.emplace_back();
  return fields;
}

bool receiveAll(int socket, char *data, std::size_t length) {
  while (length > 0) {
    const auto received = recv(socket, data, length, 0);
    if (received <= 0) return false;
    data += received;
    length -= static_cast<std::size_t>(received);
  }
  return true;
}

bool sendAll(int socket, const char *data, std::size_t length) {
  while (length > 0) {
    const auto sent = send(socket, data, length, 0);
    if (sent <= 0) return false;
    data += sent;
    length -= static_cast<std::size_t>(sent);
  }
  return true;
}

uint16_t read16(const unsigned char *data) {
  return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

uint32_t read32(const unsigned char *data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) | data[3];
}

uint64_t read64(const unsigned char *data) {
  uint64_t value = 0;
  for (int index = 0; index < 8; ++index) value = (value << 8) | data[index];
  return value;
}

void write16(unsigned char *data, uint16_t value) {
  data[0] = static_cast<unsigned char>(value >> 8);
  data[1] = static_cast<unsigned char>(value);
}

void write32(unsigned char *data, uint32_t value) {
  for (int index = 3; index >= 0; --index) {
    data[index] = static_cast<unsigned char>(value);
    value >>= 8;
  }
}

void write64(unsigned char *data, uint64_t value) {
  for (int index = 7; index >= 0; --index) {
    data[index] = static_cast<unsigned char>(value);
    value >>= 8;
  }
}

void sendFrame(int socket, uint16_t type, uint64_t requestId,
               const std::string &metadata, const char *body = nullptr,
               std::size_t bodyLength = 0) {
  unsigned char header[kHeaderSize]{};
  std::copy_n("WAD1", 4, header);
  write16(header + 4, kVersion);
  write16(header + 6, type);
  write64(header + 8, requestId);
  write32(header + 16, static_cast<uint32_t>(metadata.size()));
  write64(header + 20, bodyLength);
  sendAll(socket, reinterpret_cast<const char *>(header), sizeof(header));
  sendAll(socket, metadata.data(), metadata.size());
  if (bodyLength > 0) sendAll(socket, body, bodyLength);
}

std::size_t configuredSize(const char *name, std::size_t fallback) {
  const char *value = std::getenv(name);
  if (!value) return fallback;
  std::size_t parsed = 0;
  const auto result = std::stoull(value, &parsed);
  if (parsed != std::string(value).size()) throw std::runtime_error(std::string("invalid ") + name);
  return static_cast<std::size_t>(result);
}

Request readRequest(int socket) {
  unsigned char header[kHeaderSize];
  if (!receiveAll(socket, reinterpret_cast<char *>(header), sizeof(header))) {
    throw std::runtime_error("connection closed before request header");
  }
  if (!std::equal(header, header + 4, reinterpret_cast<const unsigned char *>("WAD1")) ||
      read16(header + 4) != kVersion) {
    throw std::runtime_error("unsupported WAD protocol header");
  }
  const auto metadataLength = read32(header + 16);
  const auto bodyLength = read64(header + 20);
  const auto maxBody = configuredSize("WAD_MAX_WRITE_BYTES", 25 * 1024 * 1024);
  if (metadataLength > kMaxMetadataSize) throw std::runtime_error("request metadata is too large");
  if (bodyLength > maxBody || bodyLength > std::numeric_limits<std::size_t>::max()) {
    throw std::runtime_error("request body is too large");
  }
  std::string metadata(metadataLength, '\0');
  std::vector<char> body(static_cast<std::size_t>(bodyLength));
  if (!receiveAll(socket, metadata.data(), metadata.size()) ||
      !receiveAll(socket, body.data(), body.size())) {
    throw std::runtime_error("connection closed before request was complete");
  }
  return {read16(header + 6), read64(header + 8), splitFields(metadata), std::move(body)};
}

bool validArtifactId(const std::string &id) {
  return !id.empty() && id.size() <= 64 &&
      std::all_of(id.begin(), id.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '-';
      });
}

std::filesystem::path artifactDirectory(const std::filesystem::path &dataRoot,
                                        const std::string &id) {
  if (!validArtifactId(id)) throw std::runtime_error("invalid artifact ID");
  return dataRoot / "artifacts" / id;
}

std::shared_ptr<std::shared_mutex> lockFor(const std::string &id) {
  std::lock_guard<std::mutex> guard(locksMutex);
  auto &lock = artifactLocks[id];
  if (!lock) lock = std::make_shared<std::shared_mutex>();
  return lock;
}

std::unique_ptr<Wad> openWad(const std::filesystem::path &path) {
  auto wad = std::unique_ptr<Wad>(Wad::loadWad(path.string()));
  if (!wad || !wad->isValid()) throw std::runtime_error("invalid WAD file");
  return wad;
}

std::size_t parseSize(const std::string &value, const std::string &field) {
  std::size_t parsed = 0;
  unsigned long long result;
  try { result = std::stoull(value, &parsed); } catch (...) {
    throw std::runtime_error("invalid " + field);
  }
  if (parsed != value.size() || result > std::numeric_limits<int>::max()) {
    throw std::runtime_error("invalid " + field);
  }
  return static_cast<std::size_t>(result);
}

void replaceAtomically(const std::filesystem::path &target,
                       const std::function<void(Wad &)> &mutation) {
  const auto next = target.string() + ".next";
  std::error_code cleanupError;
  std::filesystem::remove(next, cleanupError);
  try {
    std::filesystem::copy_file(target, next, std::filesystem::copy_options::overwrite_existing);
    {
      auto wad = openWad(next);
      mutation(*wad);
    }
    openWad(next);
    std::filesystem::rename(next, target);
  } catch (...) {
    std::filesystem::remove(next, cleanupError);
    throw;
  }
}

std::string entryJson(Wad &wad, const std::string &path) {
  const int type = wad.getType(path);
  if (type == -1) throw std::runtime_error("WAD path does not exist: " + path);
  const std::string name = path == "/" ? "/" : std::filesystem::path(path).filename().string();
  const std::string kind = path == "/" ? "root" : type == 0 ? "content" : type == 1 ? "map" : "namespace";
  std::ostringstream json;
  json << "{\"kind\":\"" << kind << "\",\"name\":\"" << jsonEscape(name)
       << "\",\"path\":\"" << jsonEscape(path) << "\"";
  if (type == 0) json << ",\"sizeBytes\":" << wad.getSize(path);
  else {
    std::vector<std::string> children;
    if (wad.getDirectory(path, &children) < 0) throw std::runtime_error("WAD path is not a directory: " + path);
    json << ",\"childrenCount\":" << children.size();
  }
  return json.str() + "}";
}

std::string childPath(const std::string &parent, const std::string &name) {
  return parent == "/" ? "/" + name : parent + "/" + name;
}

std::string treeJson(Wad &wad, const std::string &path) {
  std::ostringstream json;
  json << "{\"entry\":" << entryJson(wad, path) << ",\"children\":[";
  if (wad.isDirectory(path)) {
    std::vector<std::string> children;
    wad.getDirectory(path, &children);
    for (std::size_t index = 0; index < children.size(); ++index) {
      if (index) json << ',';
      json << treeJson(wad, childPath(path, children[index]));
    }
  }
  return json.str() + "]}";
}

std::string listJson(Wad &wad, const std::string &path) {
  std::vector<std::string> children;
  if (wad.getDirectory(path, &children) < 0) throw std::runtime_error("WAD path is not a directory: " + path);
  std::ostringstream json;
  json << "{\"path\":\"" << jsonEscape(path) << "\",\"entries\":[";
  for (std::size_t index = 0; index < children.size(); ++index) {
    if (index) json << ',';
    json << entryJson(wad, childPath(path, children[index]));
  }
  return json.str() + "]}";
}

std::string inspectionJson(const std::filesystem::path &path, Wad &wad) {
  std::ostringstream json;
  json << "{\"valid\":true,\"magic\":\"" << wad.getMagic()
       << "\",\"descriptorCount\":" << wad.getDescriptorCount()
       << ",\"descriptorOffset\":" << wad.getDescriptorOffset()
       << ",\"fileSizeBytes\":" << std::filesystem::file_size(path) << "}";
  return json.str();
}

void createArtifact(const std::filesystem::path &dataRoot, const Request &request) {
  if (request.fields.empty()) throw std::runtime_error("CREATE_ARTIFACT requires an artifact ID");
  const auto directory = artifactDirectory(dataRoot, request.fields[0]);
  const auto original = directory / "original.wad";
  const auto working = directory / "working.wad";
  const auto next = directory.string() + ".next";
  std::error_code error;
  std::filesystem::remove_all(next, error);
  if (std::filesystem::exists(directory)) throw std::runtime_error("artifact already exists");
  try {
    std::filesystem::create_directories(next);
    std::ofstream output(std::filesystem::path(next) / "original.wad", std::ios::binary);
    output.write(request.body.data(), static_cast<std::streamsize>(request.body.size()));
    output.close();
    openWad(std::filesystem::path(next) / "original.wad");
    std::filesystem::copy_file(std::filesystem::path(next) / "original.wad",
                               std::filesystem::path(next) / "working.wad");
    std::filesystem::rename(next, directory);
  } catch (...) {
    std::filesystem::remove_all(next, error);
    throw;
  }
  (void)original;
  (void)working;
}

std::vector<char> readFile(const std::filesystem::path &path) {
  const auto length = std::filesystem::file_size(path);
  std::vector<char> body(static_cast<std::size_t>(length));
  std::ifstream input(path, std::ios::binary);
  input.read(body.data(), static_cast<std::streamsize>(body.size()));
  if (!input && !body.empty()) throw std::runtime_error("could not read WAD file");
  return body;
}

void handleConnection(int client, const std::filesystem::path &dataRoot) {
  uint64_t requestId = 0;
  try {
    const auto request = readRequest(client);
    requestId = request.id;
    if (request.type == PING) {
      sendFrame(client, kSuccess, requestId, "{\"pong\":true}");
      return;
    }
    if (request.fields.empty()) throw std::runtime_error("missing artifact ID");
    const auto id = request.fields[0];
    const auto lock = lockFor(id);
    const auto directory = artifactDirectory(dataRoot, id);
    const auto original = directory / "original.wad";
    const auto working = directory / "working.wad";

    const bool mutating = request.type == CREATE_ARTIFACT || request.type == MKDIR ||
        request.type == PUT || request.type == RESET || request.type == DELETE_ARTIFACT;
    std::unique_lock<std::shared_mutex> writeLock(*lock, std::defer_lock);
    std::shared_lock<std::shared_mutex> readLock(*lock, std::defer_lock);
    if (mutating) writeLock.lock(); else readLock.lock();

    if (request.type == CREATE_ARTIFACT) {
      createArtifact(dataRoot, request);
      auto wad = openWad(working);
      sendFrame(client, kSuccess, requestId, inspectionJson(working, *wad));
      return;
    }
    if (!std::filesystem::exists(working)) throw std::runtime_error("artifact does not exist");
    if (request.type == DELETE_ARTIFACT) {
      std::filesystem::remove_all(directory);
      sendFrame(client, kSuccess, requestId, "{\"removed\":true}");
      return;
    }
    if (request.type == RESET) {
      const auto next = working.string() + ".next";
      std::filesystem::copy_file(original, next, std::filesystem::copy_options::overwrite_existing);
      openWad(next);
      std::filesystem::rename(next, working);
      sendFrame(client, kSuccess, requestId, "{\"reset\":true}");
      return;
    }
    if (request.type == MKDIR || request.type == PUT) {
      if (request.fields.size() != 2) throw std::runtime_error("mutation requires a virtual path");
      replaceAtomically(working, [&](Wad &wad) {
        if (request.type == MKDIR) {
          if (!wad.createDirectory(request.fields[1])) throw std::runtime_error("could not create namespace: check its name, parent, and whether it already exists");
        } else {
          if (!wad.createFile(request.fields[1])) throw std::runtime_error("could not create item: check its name, parent, and whether it already exists");
          if (!request.body.empty() && wad.writeToFile(request.fields[1], request.body.data(), static_cast<int>(request.body.size()), 0) != static_cast<int>(request.body.size())) {
            throw std::runtime_error("could not write item payload");
          }
        }
      });
      auto wad = openWad(working);
      sendFrame(client, kSuccess, requestId, entryJson(*wad, request.fields[1]));
      return;
    }
    if (request.type == DOWNLOAD) {
      const auto body = readFile(working);
      sendFrame(client, kSuccess, requestId, "{}", body.data(), body.size());
      return;
    }

    auto wad = openWad(working);
    const std::string path = request.fields.size() > 1 && !request.fields[1].empty() ? request.fields[1] : "/";
    if (request.type == INSPECT) sendFrame(client, kSuccess, requestId, inspectionJson(working, *wad));
    else if (request.type == TREE) sendFrame(client, kSuccess, requestId, treeJson(*wad, "/"));
    else if (request.type == LIST) sendFrame(client, kSuccess, requestId, listJson(*wad, path));
    else if (request.type == STAT) sendFrame(client, kSuccess, requestId, entryJson(*wad, path));
    else if (request.type == READ || request.type == READ_RANGE) {
      const int size = wad->getSize(path);
      if (size < 0) throw std::runtime_error("WAD path is not content: " + path);
      const auto offset = request.type == READ_RANGE && request.fields.size() > 2 ? parseSize(request.fields[2], "read offset") : 0;
      const auto requested = request.type == READ_RANGE && request.fields.size() > 3 ? parseSize(request.fields[3], "read length") : static_cast<std::size_t>(size);
      std::vector<char> body(requested);
      const int count = wad->getContents(path, body.data(), static_cast<int>(requested), static_cast<int>(offset));
      if (count < 0) throw std::runtime_error("could not read WAD content");
      sendFrame(client, kSuccess, requestId, "{}", body.data(), static_cast<std::size_t>(count));
    } else throw std::runtime_error("unknown command");
  } catch (const std::exception &error) {
    sendFrame(client, kError, requestId,
              "{\"error\":{\"code\":\"WAD_OPERATION_FAILED\",\"message\":\"" +
                  jsonEscape(error.what()) + "\"}}");
  }
}

}  // namespace

int main() {
  signal(SIGPIPE, SIG_IGN);
  const char *configuredPort = std::getenv("WAD_LISTEN_PORT") ? std::getenv("WAD_LISTEN_PORT") : std::getenv("PORT");
  const int port = configuredPort ? std::stoi(configuredPort) : 7373;
  const auto dataRoot = std::filesystem::weakly_canonical(std::getenv("WAD_DATA_DIR") ? std::getenv("WAD_DATA_DIR") : "./data");
  std::filesystem::create_directories(dataRoot / "artifacts");

  const int server = socket(AF_INET6, SOCK_STREAM, 0);
  if (server < 0) throw std::runtime_error("could not create TCP socket");
  int reuse = 1;
  int ipv6Only = 0;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  setsockopt(server, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6Only, sizeof(ipv6Only));
  sockaddr_in6 address{};
  address.sin6_family = AF_INET6;
  address.sin6_addr = in6addr_any;
  address.sin6_port = htons(static_cast<uint16_t>(port));
  if (::bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 || ::listen(server, 64) < 0) {
    close(server);
    throw std::runtime_error("could not bind WAD TCP server");
  }

  const auto workerCount = configuredSize("WAD_WORKERS", 8);
  const auto maxQueued = configuredSize("WAD_MAX_QUEUED_CONNECTIONS", 128);
  std::mutex queueMutex;
  std::condition_variable queueReady;
  std::queue<int> clients;
  std::vector<std::thread> workers;
  for (std::size_t index = 0; index < workerCount; ++index) {
    workers.emplace_back([&] {
      while (true) {
        int client;
        {
          std::unique_lock<std::mutex> lock(queueMutex);
          queueReady.wait(lock, [&] { return !clients.empty(); });
          client = clients.front();
          clients.pop();
        }
        handleConnection(client, dataRoot);
        close(client);
      }
    });
  }

  std::cout << "WAD server listening on port " << port << " with data in " << dataRoot
            << " using " << workerCount << " workers" << std::endl;
  while (true) {
    const int client = accept(server, nullptr, nullptr);
    if (client < 0) continue;
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      if (clients.size() >= maxQueued) {
        close(client);
        continue;
      }
      clients.push(client);
    }
    queueReady.notify_one();
  }
}
