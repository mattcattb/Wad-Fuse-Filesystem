import {afterAll, describe, expect, test} from "bun:test";
import {mkdtemp, rm} from "node:fs/promises";
import {createConnection, createServer} from "node:net";
import {tmpdir} from "node:os";
import {join, resolve} from "node:path";
import {WadClient, WadClientError} from "../src";

const wadRoot = resolve(import.meta.dir, "../../..");
const dataDirectory = await mkdtemp(join(tmpdir(), "wad-client-integration-"));

const reservePort = async () => {
  const server = createServer();
  await new Promise<void>((resolveListen, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolveListen);
  });
  const address = server.address();
  if (!address || typeof address === "string") {
    throw new Error("Unable to reserve an integration-test port");
  }
  await new Promise<void>((resolveClose, reject) =>
    server.close((error) => (error ? reject(error) : resolveClose())),
  );
  return address.port;
};

const waitForPort = async (port: number) => {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    const connected = await new Promise<boolean>((resolveConnection) => {
      const socket = createConnection({host: "127.0.0.1", port});
      socket.once("connect", () => {
        socket.destroy();
        resolveConnection(true);
      });
      socket.once("error", () => resolveConnection(false));
    });
    if (connected) return;
    await Bun.sleep(20);
  }
  throw new Error("WAD runtime did not start");
};

const port = await reservePort();
const runtime = Bun.spawn(["./wadsrv-bin"], {
  cwd: wadRoot,
  env: {
    ...process.env,
    WAD_LISTEN_PORT: String(port),
    WAD_DATA_DIR: dataDirectory,
  },
  stdout: "ignore",
  stderr: "inherit",
});
await waitForPort(port);

afterAll(async () => {
  runtime.kill();
  await runtime.exited;
  await rm(dataDirectory, {recursive: true, force: true});
});

describe("WAD client and C++ runtime", () => {
  test("creates, explores, downloads, resets, and deletes an artifact", async () => {
    const client = new WadClient({
      endpoint: `wad://127.0.0.1:${port}`,
    });
    const artifactId = "integration-artifact";
    const source = new Uint8Array(
      await Bun.file(resolve(wadRoot, "examples/sample1.wad")).arrayBuffer(),
    );

    try {
      await expect(client.ping()).resolves.toEqual({pong: true});
      const created = await client.createArtifact(artifactId, source);
      expect(created).toMatchObject({valid: true, magic: "IWAD"});

      const tree = await client.tree(artifactId);
      expect(tree.entry).toMatchObject({kind: "root", path: "/"});

      const root = await client.list(artifactId);
      expect(root.entries.some((entry) => entry.path === "/mp.txt")).toBe(true);
      expect(await client.stat(artifactId, "/mp.txt")).toMatchObject({
        kind: "content",
        sizeBytes: 398,
      });

      const contents = await client.read(artifactId, "/mp.txt");
      expect(new TextDecoder().decode(contents)).toContain("Arthur");
      expect(new TextDecoder().decode(
        await client.readRange(artifactId, "/mp.txt", 0, 4),
      )).toBe("WHAT");

      await client.createNamespace(artifactId, "/TX");
      const note = new TextEncoder().encode("hello from Bun");
      expect(await client.put(artifactId, "/TX/NOTE", note)).toMatchObject({
        kind: "content",
        path: "/TX/NOTE",
        sizeBytes: note.byteLength,
      });
      expect(await client.read(artifactId, "/TX/NOTE")).toEqual(note);

      const modified = await client.tree(artifactId);
      expect(modified.children.some((entry) => entry.entry.path === "/TX")).toBe(
        true,
      );

      await client.reset(artifactId);
      const downloaded = await client.download(artifactId);
      expect(downloaded).toEqual(source);

      await client.deleteArtifact(artifactId);
      await expect(client.inspect(artifactId)).rejects.toMatchObject<WadClientError>(
        {kind: "runtime"},
      );
    } finally {
      client.close();
    }
  });
});
