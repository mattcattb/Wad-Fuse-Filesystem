import {describe, expect, test} from "bun:test";
import {createServer, type Server, type Socket} from "node:net";
import {WadClient, WadClientError} from "../src";

const listen = (server: Server) =>
  new Promise<number>((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      server.off("error", reject);
      const address = server.address();
      if (!address || typeof address === "string") {
        reject(new Error("Test server did not expose a TCP port"));
        return;
      }
      resolve(address.port);
    });
  });

const close = (server: Server) =>
  new Promise<void>((resolve, reject) => {
    server.close((error) => (error ? reject(error) : resolve()));
  });

const response = (
  socket: Socket,
  requestId: bigint,
  metadata: string,
) => {
  const content = Buffer.from(metadata);
  const frame = Buffer.alloc(28 + content.length);
  frame.write("WAD1", 0, "ascii");
  frame.writeUInt16BE(1, 4);
  frame.writeUInt16BE(100, 6);
  frame.writeBigUInt64BE(requestId, 8);
  frame.writeUInt32BE(content.length, 16);
  content.copy(frame, 28);
  socket.write(frame.subarray(0, 11));
  setTimeout(() => socket.end(frame.subarray(11)), 5);
};

describe("WAD client", () => {
  test("reads a framed response delivered in partial chunks", async () => {
    const server = createServer((socket) => {
      let input = Buffer.alloc(0);
      socket.on("data", (chunk) => {
        input = Buffer.concat([input, chunk]);
        if (input.length < 28) return;
        response(socket, input.readBigUInt64BE(8), '{"pong":true}');
      });
    });
    const port = await listen(server);
    const client = new WadClient({endpoint: `wad://127.0.0.1:${port}`});

    try {
      await expect(client.ping()).resolves.toEqual({pong: true});
    } finally {
      client.close();
      await close(server);
    }
  });

  test("times out without retrying the request", async () => {
    const server = createServer(() => {});
    const port = await listen(server);
    const client = new WadClient({
      endpoint: `wad://127.0.0.1:${port}`,
      requestTimeoutMs: 20,
    });

    try {
      await expect(client.ping()).rejects.toMatchObject<WadClientError>({
        kind: "timeout",
      });
    } finally {
      client.close();
      await close(server);
    }
  });
});
