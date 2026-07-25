export type WadInspection = {
  valid: true;
  magic: "IWAD" | "PWAD";
  descriptorCount: number;
  descriptorOffset: number;
  fileSizeBytes: number;
};

export type WadEntry = {
  kind: "root" | "content" | "map" | "namespace";
  name: string;
  path: string;
  sizeBytes?: number;
  childrenCount?: number;
};

export type WadTree = {
  entry: WadEntry;
  children: WadTree[];
};

export type WadDirectory = {
  path: string;
  entries: WadEntry[];
};

export type WadResponse = {
  metadata: unknown;
  body: Uint8Array<ArrayBuffer>;
};
