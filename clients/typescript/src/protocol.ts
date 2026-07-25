import {Buffer} from "node:buffer";
import {WadClientError} from "./errors.js";
import type {WadResponse} from "./types.js";

export const WAD_PROTOCOL_VERSION = 1;
export const WAD_HEADER_BYTES = 28;
const MAX_WAD_METADATA_BYTES = 16 * 1024;

export const wadCommandKinds = {
  ping: 1,
  createArtifact: 2,
  inspect: 3,
  tree: 4,
  list: 5,
  stat: 6,
  read: 7,
  readRange: 8,
  mkdir: 9,
  put: 10,
  reset: 11,
  download: 12,
  deleteArtifact: 13,
} as const;

const WAD_SUCCESS = 100;
const WAD_ERROR = 101;
const magic = Buffer.from("WAD1", "ascii");
const decoder = new TextDecoder("utf-8", {fatal: true});

export const encodeWadRequest = (
  command: number,
  requestId: bigint,
  fields: string[],
  body: Uint8Array = new Uint8Array(),
) => {
  if (
    fields.some(
      (field) =>
        field.includes("\t") ||
        field.includes("\n") ||
        field.includes("\r"),
    )
  ) {
    throw new WadClientError(
      "input",
      "WAD request fields cannot contain tabs or line breaks",
    );
  }

  const metadata = Buffer.from(fields.join("\t"), "utf8");
  if (metadata.byteLength > MAX_WAD_METADATA_BYTES) {
    throw new WadClientError("input", "WAD request metadata is too large");
  }

  const header = Buffer.alloc(WAD_HEADER_BYTES);
  magic.copy(header, 0);
  header.writeUInt16BE(WAD_PROTOCOL_VERSION, 4);
  header.writeUInt16BE(command, 6);
  header.writeBigUInt64BE(requestId, 8);
  header.writeUInt32BE(metadata.byteLength, 16);
  header.writeBigUInt64BE(BigInt(body.byteLength), 20);
  return Buffer.concat([header, metadata, body]);
};

export const readWadHeader = (input: Buffer, maxBodyBytes: number) => {
  if (input.byteLength < WAD_HEADER_BYTES) return null;
  if (!input.subarray(0, 4).equals(magic)) {
    throw new WadClientError("protocol", "Invalid WAD response magic");
  }
  const version = input.readUInt16BE(4);
  if (version !== WAD_PROTOCOL_VERSION) {
    throw new WadClientError(
      "protocol",
      `Unsupported WAD protocol version ${version}`,
    );
  }

  const type = input.readUInt16BE(6);
  if (type !== WAD_SUCCESS && type !== WAD_ERROR) {
    throw new WadClientError("protocol", `Unknown WAD response type ${type}`);
  }

  const metadataLength = input.readUInt32BE(16);
  if (metadataLength > MAX_WAD_METADATA_BYTES) {
    throw new WadClientError("protocol", "WAD response metadata is too large");
  }

  const rawBodyLength = input.readBigUInt64BE(20);
  if (
    rawBodyLength > BigInt(Number.MAX_SAFE_INTEGER) ||
    rawBodyLength > BigInt(maxBodyBytes)
  ) {
    throw new WadClientError("protocol", "WAD response body is too large");
  }

  return {
    type,
    requestId: input.readBigUInt64BE(8),
    metadataLength,
    bodyLength: Number(rawBodyLength),
    frameLength: WAD_HEADER_BYTES + metadataLength + Number(rawBodyLength),
  };
};

export const decodeWadResponse = (
  frame: Buffer,
  expectedRequestId: bigint,
  maxBodyBytes: number,
): WadResponse => {
  const header = readWadHeader(frame, maxBodyBytes);
  if (!header || frame.byteLength !== header.frameLength) {
    throw new WadClientError("protocol", "Incomplete WAD response frame");
  }
  if (header.requestId !== expectedRequestId) {
    throw new WadClientError("protocol", "WAD response request ID mismatch");
  }

  const metadataStart = WAD_HEADER_BYTES;
  const bodyStart = metadataStart + header.metadataLength;
  let metadata: unknown;
  try {
    metadata = JSON.parse(
      decoder.decode(frame.subarray(metadataStart, bodyStart)),
    );
  } catch (cause) {
    throw new WadClientError(
      "protocol",
      "WAD response metadata is not valid JSON",
      undefined,
      {cause},
    );
  }

  if (header.type === WAD_ERROR) {
    const error =
      typeof metadata === "object" &&
      metadata !== null &&
      "error" in metadata &&
      typeof metadata.error === "object" &&
      metadata.error !== null
        ? metadata.error
        : null;
    const code =
      error && "code" in error && typeof error.code === "string"
        ? error.code
        : "WAD_OPERATION_FAILED";
    const message =
      error && "message" in error && typeof error.message === "string"
        ? error.message
        : "WAD operation failed";
    throw new WadClientError("runtime", message, code);
  }

  return {
    metadata,
    body: Uint8Array.from(frame.subarray(bodyStart)),
  };
};
