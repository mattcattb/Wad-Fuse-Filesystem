import {Buffer} from "node:buffer";
import type {z} from "zod";
import {WadClientError} from "./errors.js";
import {
  wadArtifactIdSchema,
  wadDirectorySchema,
  wadEntrySchema,
  wadInspectionSchema,
  wadPathSchema,
  wadPongSchema,
  wadRemovedSchema,
  wadResetSchema,
  wadTreeSchema,
} from "./messages.js";
import {
  decodeWadResponse,
  encodeWadRequest,
  readWadHeader,
  wadCommandKinds,
} from "./protocol.js";

export type WadClientOptions = {
  endpoint: string;
  requestTimeoutMs?: number;
  maxBodyBytes?: number;
};

type WadSocketState = {
  input: Buffer;
  outgoing: Buffer;
  outgoingOffset: number;
  requestId: bigint;
  settled: boolean;
  timeout?: ReturnType<typeof setTimeout>;
  resolve: (value: ReturnType<typeof decodeWadResponse>) => void;
  reject: (error: Error) => void;
};

export class WadClient {
  private readonly endpoint: URL;
  private readonly requestTimeoutMs: number;
  private readonly maxBodyBytes: number;
  private readonly sockets = new Set<Bun.Socket<WadSocketState>>();
  private readonly pending = new Set<WadSocketState>();
  private nextRequestId = 1n;
  private closed = false;

  constructor(options: WadClientOptions) {
    try {
      this.endpoint = new URL(options.endpoint);
    } catch (cause) {
      throw new WadClientError(
        "configuration",
        "Invalid WAD endpoint",
        undefined,
        {cause},
      );
    }
    if (
      this.endpoint.protocol !== "wad:" ||
      !this.endpoint.hostname ||
      !this.endpoint.port
    ) {
      throw new WadClientError(
        "configuration",
        "WAD endpoint must be wad://host:port",
      );
    }

    this.requestTimeoutMs = options.requestTimeoutMs ?? 5_000;
    this.maxBodyBytes = options.maxBodyBytes ?? 25 * 1024 * 1024;
    if (
      !Number.isFinite(this.requestTimeoutMs) ||
      this.requestTimeoutMs <= 0 ||
      !Number.isSafeInteger(this.maxBodyBytes) ||
      this.maxBodyBytes <= 0
    ) {
      throw new WadClientError(
        "configuration",
        "WAD timeout and maximum body size must be positive",
      );
    }
  }

  async ping() {
    const response = await this.request(wadCommandKinds.ping, []);
    return this.metadata(wadPongSchema, response.metadata);
  }

  async createArtifact(artifactId: string, body: Uint8Array) {
    this.validateBody(body);
    const response = await this.request(
      wadCommandKinds.createArtifact,
      [this.artifactId(artifactId)],
      body,
    );
    return this.metadata(wadInspectionSchema, response.metadata);
  }

  async inspect(artifactId: string) {
    const response = await this.request(wadCommandKinds.inspect, [
      this.artifactId(artifactId),
    ]);
    return this.metadata(wadInspectionSchema, response.metadata);
  }

  async tree(artifactId: string) {
    const response = await this.request(wadCommandKinds.tree, [
      this.artifactId(artifactId),
    ]);
    return this.metadata(wadTreeSchema, response.metadata);
  }

  async list(artifactId: string, path = "/") {
    const response = await this.request(wadCommandKinds.list, [
      this.artifactId(artifactId),
      this.path(path),
    ]);
    return this.metadata(wadDirectorySchema, response.metadata);
  }

  async stat(artifactId: string, path: string) {
    const response = await this.request(wadCommandKinds.stat, [
      this.artifactId(artifactId),
      this.path(path),
    ]);
    return this.metadata(wadEntrySchema, response.metadata);
  }

  async read(artifactId: string, path: string) {
    const response = await this.request(wadCommandKinds.read, [
      this.artifactId(artifactId),
      this.path(path),
    ]);
    return response.body;
  }

  async readRange(
    artifactId: string,
    path: string,
    offset: number,
    length: number,
  ) {
    if (
      !Number.isSafeInteger(offset) ||
      offset < 0 ||
      !Number.isSafeInteger(length) ||
      length <= 0
    ) {
      throw new WadClientError(
        "input",
        "WAD byte range must use a nonnegative offset and positive length",
      );
    }
    const response = await this.request(wadCommandKinds.readRange, [
      this.artifactId(artifactId),
      this.path(path),
      String(offset),
      String(length),
    ]);
    return response.body;
  }

  async createNamespace(artifactId: string, path: string) {
    const response = await this.request(wadCommandKinds.mkdir, [
      this.artifactId(artifactId),
      this.path(path),
    ]);
    return this.metadata(wadEntrySchema, response.metadata);
  }

  async put(artifactId: string, path: string, body = new Uint8Array()) {
    this.validateBody(body);
    const response = await this.request(
      wadCommandKinds.put,
      [this.artifactId(artifactId), this.path(path)],
      body,
    );
    return this.metadata(wadEntrySchema, response.metadata);
  }

  async reset(artifactId: string) {
    const response = await this.request(wadCommandKinds.reset, [
      this.artifactId(artifactId),
    ]);
    return this.metadata(wadResetSchema, response.metadata);
  }

  async download(artifactId: string) {
    const response = await this.request(wadCommandKinds.download, [
      this.artifactId(artifactId),
    ]);
    return response.body;
  }

  async deleteArtifact(artifactId: string) {
    const response = await this.request(wadCommandKinds.deleteArtifact, [
      this.artifactId(artifactId),
    ]);
    return this.metadata(wadRemovedSchema, response.metadata);
  }

  close() {
    if (this.closed) return;
    this.closed = true;
    const error = new WadClientError("closed", "WAD client was closed");
    for (const request of this.pending) {
      request.settled = true;
      if (request.timeout) clearTimeout(request.timeout);
      request.reject(error);
    }
    this.pending.clear();
    for (const socket of [...this.sockets]) socket.close();
    this.sockets.clear();
  }

  private request(
    command: number,
    fields: string[],
    body: Uint8Array = new Uint8Array(),
  ) {
    if (this.closed) {
      return Promise.reject(
        new WadClientError("closed", "WAD client is closed"),
      );
    }

    const requestId = this.nextRequestId;
    this.nextRequestId += 1n;
    const outgoing = encodeWadRequest(command, requestId, fields, body);

    return new Promise<ReturnType<typeof decodeWadResponse>>(
      (resolve, reject) => {
        const state: WadSocketState = {
          input: Buffer.alloc(0),
          outgoing,
          outgoingOffset: 0,
          requestId,
          settled: false,
          resolve,
          reject,
        };
        this.pending.add(state);
        let activeSocket: Bun.Socket<WadSocketState> | undefined;
        state.timeout = setTimeout(() => {
          if (activeSocket) {
            this.fail(
              activeSocket,
              new WadClientError(
                "timeout",
                "WAD request timed out; the operation may have completed",
              ),
            );
          } else if (!state.settled) {
            state.settled = true;
            this.pending.delete(state);
            reject(
              new WadClientError(
                "timeout",
                "WAD request timed out; the operation may have completed",
              ),
            );
          }
        }, this.requestTimeoutMs);

        void Bun.connect<WadSocketState>({
          hostname: this.endpoint.hostname,
          port: Number(this.endpoint.port),
          data: state,
          socket: {
            open: (socket) => {
              activeSocket = socket;
              if (this.closed || socket.data.settled) {
                if (!socket.data.settled) {
                  this.fail(
                    socket,
                    new WadClientError("closed", "WAD client was closed"),
                  );
                }
                socket.close();
                return;
              }
              this.sockets.add(socket);
              this.flush(socket);
            },
            data: (socket, bytes) => this.receive(socket, bytes),
            drain: (socket) => this.flush(socket),
            error: (socket, cause) =>
              this.fail(
                socket,
                new WadClientError(
                  "connection",
                  "Unable to communicate with the WAD runtime",
                  undefined,
                  {cause},
                ),
              ),
            close: (socket) => {
              if (!socket.data.settled) {
                this.fail(
                  socket,
                  new WadClientError(
                    "connection",
                    "WAD runtime closed the connection early",
                  ),
                );
              }
            },
          },
        }).catch((cause: unknown) => {
          if (state.settled) return;
          state.settled = true;
          this.pending.delete(state);
          if (state.timeout) clearTimeout(state.timeout);
          reject(
            new WadClientError(
              "connection",
              "Unable to connect to the WAD runtime",
              undefined,
              {cause},
            ),
          );
        });
      },
    );
  }

  private flush(socket: Bun.Socket<WadSocketState>) {
    const {outgoing} = socket.data;
    if (socket.data.outgoingOffset >= outgoing.byteLength) return;
    const written = socket.write(
      outgoing,
      socket.data.outgoingOffset,
      outgoing.byteLength - socket.data.outgoingOffset,
    );
    if (written < 0) {
      this.fail(
        socket,
        new WadClientError("connection", "WAD socket is closed"),
      );
      return;
    }
    socket.data.outgoingOffset += written;
  }

  private receive(socket: Bun.Socket<WadSocketState>, bytes: Buffer) {
    socket.data.input = Buffer.concat([socket.data.input, bytes]);

    let header;
    try {
      header = readWadHeader(socket.data.input, this.maxBodyBytes);
    } catch (cause) {
      this.fail(
        socket,
        cause instanceof Error
          ? cause
          : new WadClientError("protocol", "Invalid WAD response"),
      );
      return;
    }
    if (!header || socket.data.input.byteLength < header.frameLength) return;
    if (socket.data.input.byteLength !== header.frameLength) {
      this.fail(
        socket,
        new WadClientError("protocol", "WAD response has trailing bytes"),
      );
      return;
    }

    try {
      const response = decodeWadResponse(
        socket.data.input,
        socket.data.requestId,
        this.maxBodyBytes,
      );
      this.finish(socket, response);
    } catch (cause) {
      this.fail(
        socket,
        cause instanceof Error
          ? cause
          : new WadClientError("protocol", "Unable to decode WAD response"),
      );
    }
  }

  private finish(
    socket: Bun.Socket<WadSocketState>,
    response: ReturnType<typeof decodeWadResponse>,
  ) {
    if (socket.data.settled) return;
    socket.data.settled = true;
    if (socket.data.timeout) clearTimeout(socket.data.timeout);
    this.pending.delete(socket.data);
    this.sockets.delete(socket);
    socket.data.resolve(response);
    socket.close();
  }

  private fail(socket: Bun.Socket<WadSocketState>, error: Error) {
    if (socket.data.settled) return;
    socket.data.settled = true;
    if (socket.data.timeout) clearTimeout(socket.data.timeout);
    this.pending.delete(socket.data);
    this.sockets.delete(socket);
    socket.data.reject(error);
    socket.close();
  }

  private artifactId(value: string) {
    const result = wadArtifactIdSchema.safeParse(value);
    if (!result.success) {
      throw new WadClientError(
        "input",
        result.error.issues[0]?.message ?? "Invalid WAD artifact ID",
        undefined,
        {cause: result.error},
      );
    }
    return result.data;
  }

  private path(value: string) {
    const result = wadPathSchema.safeParse(value);
    if (!result.success) {
      throw new WadClientError(
        "input",
        result.error.issues[0]?.message ?? "Invalid WAD path",
        undefined,
        {cause: result.error},
      );
    }
    return result.data;
  }

  private validateBody(body: Uint8Array) {
    if (body.byteLength > this.maxBodyBytes) {
      throw new WadClientError("input", "WAD request body is too large");
    }
  }

  private metadata<T>(schema: z.ZodType<T>, value: unknown) {
    const result = schema.safeParse(value);
    if (!result.success) {
      throw new WadClientError(
        "protocol",
        "WAD runtime returned invalid response metadata",
        undefined,
        {cause: result.error},
      );
    }
    return result.data;
  }
}
