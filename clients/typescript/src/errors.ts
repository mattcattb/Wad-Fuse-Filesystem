export type WadClientErrorKind =
  | "configuration"
  | "input"
  | "connection"
  | "timeout"
  | "protocol"
  | "runtime"
  | "closed";

export class WadClientError extends Error {
  readonly name = "WadClientError";

  constructor(
    readonly kind: WadClientErrorKind,
    message: string,
    readonly code?: string,
    options?: ErrorOptions,
  ) {
    super(message, options);
  }
}
