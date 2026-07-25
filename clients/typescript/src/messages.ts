import {z} from "zod";
import type {
  WadDirectory,
  WadEntry,
  WadInspection,
  WadTree,
} from "./types.js";

export const wadArtifactIdSchema = z
  .string()
  .min(1)
  .max(64)
  .regex(/^[a-zA-Z0-9-]+$/, "Invalid WAD artifact ID");

export const wadPathSchema = z.string().startsWith("/").max(1024);

export const wadInspectionSchema = z
  .object({
    valid: z.literal(true),
    magic: z.enum(["IWAD", "PWAD"]),
    descriptorCount: z.number().int().nonnegative(),
    descriptorOffset: z.number().int().nonnegative(),
    fileSizeBytes: z.number().int().nonnegative(),
  })
  .strict() satisfies z.ZodType<WadInspection>;

export const wadEntrySchema = z
  .object({
    kind: z.enum(["root", "content", "map", "namespace"]),
    name: z.string(),
    path: z.string(),
    sizeBytes: z.number().int().nonnegative().optional(),
    childrenCount: z.number().int().nonnegative().optional(),
  })
  .strict() satisfies z.ZodType<WadEntry>;

export const wadTreeSchema: z.ZodType<WadTree> = z.lazy(() =>
  z
    .object({
      entry: wadEntrySchema,
      children: z.array(wadTreeSchema),
    })
    .strict(),
);

export const wadDirectorySchema = z
  .object({
    path: z.string(),
    entries: z.array(wadEntrySchema),
  })
  .strict() satisfies z.ZodType<WadDirectory>;

export const wadPongSchema = z.object({pong: z.literal(true)}).strict();
export const wadResetSchema = z.object({reset: z.literal(true)}).strict();
export const wadRemovedSchema = z.object({removed: z.literal(true)}).strict();
