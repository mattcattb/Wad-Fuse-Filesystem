# `@break-my-system/wad-client`

Server-side TypeScript client for the WAD filesystem runtime.

```ts
import {WadClient} from "@break-my-system/wad-client";

const client = new WadClient({
  endpoint: "wad://127.0.0.1:7373",
});

const inspection = await client.createArtifact("example", wadBytes);
const tree = await client.tree("example");

client.close();
```

The runtime accepts one request per TCP connection. The client owns binary
framing, request IDs, timeouts, response validation, and connection cleanup;
applications own artifact metadata and their HTTP-facing behavior.
