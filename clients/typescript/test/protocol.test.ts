import {describe, expect, test} from "bun:test";
import {
  decodeWadResponse,
  encodeWadRequest,
  WAD_HEADER_BYTES,
  wadCommandKinds,
} from "../src/protocol";

describe("WAD protocol", () => {
  test("encodes the C++ ping golden vector", () => {
    expect([
      ...encodeWadRequest(wadCommandKinds.ping, 1n, []),
    ]).toEqual([
      ..."WAD1".split("").map((value) => value.charCodeAt(0)),
      0, 1,
      0, 1,
      0, 0, 0, 0, 0, 0, 0, 1,
      0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,
    ]);
  });

  test("decodes response metadata and body", () => {
    const metadata = Buffer.from('{"pong":true}');
    const body = Buffer.from("bytes");
    const frame = Buffer.alloc(WAD_HEADER_BYTES + metadata.length + body.length);
    frame.write("WAD1", 0, "ascii");
    frame.writeUInt16BE(1, 4);
    frame.writeUInt16BE(100, 6);
    frame.writeBigUInt64BE(7n, 8);
    frame.writeUInt32BE(metadata.length, 16);
    frame.writeBigUInt64BE(BigInt(body.length), 20);
    metadata.copy(frame, WAD_HEADER_BYTES);
    body.copy(frame, WAD_HEADER_BYTES + metadata.length);

    expect(decodeWadResponse(frame, 7n, 1024)).toEqual({
      metadata: {pong: true},
      body: Uint8Array.from(body),
    });
  });

  test("decodes a runtime error", () => {
    const metadata = Buffer.from(
      '{"error":{"code":"WAD_OPERATION_FAILED","message":"invalid WAD"}}',
    );
    const frame = Buffer.alloc(WAD_HEADER_BYTES + metadata.length);
    frame.write("WAD1", 0, "ascii");
    frame.writeUInt16BE(1, 4);
    frame.writeUInt16BE(101, 6);
    frame.writeBigUInt64BE(3n, 8);
    frame.writeUInt32BE(metadata.length, 16);
    metadata.copy(frame, WAD_HEADER_BYTES);

    expect(() => decodeWadResponse(frame, 3n, 1024)).toThrow("invalid WAD");
  });
});
