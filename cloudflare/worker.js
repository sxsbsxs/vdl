import { DurableObject } from "cloudflare:workers";

const CONTAINER_PORT = 10000;
const INACTIVITY_TIMEOUT_MS = 5 * 60 * 1000;

export class VideoDLContainer extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.starting = undefined;
    if (ctx.container?.running) {
      void ctx.blockConcurrencyWhile(() => ctx.container.setInactivityTimeout(INACTIVITY_TIMEOUT_MS));
    }
  }

  async fetch(request) {
    this.starting ??= this.startAndWaitForPort().finally(() => { this.starting = undefined; });
    await this.starting;

    const url = new URL(request.url);
    url.protocol = "http:";
    url.host = "container";
    const headers = new Headers(request.headers);
    headers.delete("host");
    // Do not trust a caller-supplied forwarding chain for the C++ rate limiter.
    const clientIp = request.headers.get("CF-Connecting-IP");
    if (clientIp) headers.set("X-Forwarded-For", clientIp);
    const forwarded = new Request(url, {
      method: request.method,
      headers,
      body: ["GET", "HEAD"].includes(request.method) ? undefined : request.body,
      redirect: "manual"
    });
    return this.ctx.container.getTcpPort(CONTAINER_PORT).fetch(forwarded);
  }

  async startAndWaitForPort() {
    const container = this.ctx.container;
    if (!container.running) {
      container.start({
        image: container.images.base,
        instance: "standard-1",
        enableInternet: true
      });
    }
    await container.setInactivityTimeout(INACTIVITY_TIMEOUT_MS);
    const port = container.getTcpPort(CONTAINER_PORT);
    for (let attempt = 0; attempt < 120; attempt++) {
      try {
        const response = await port.fetch("http://container/api/health", {
          signal: AbortSignal.timeout(1000)
        });
        await response.body?.cancel();
        if (response.ok) return;
      } catch (_) {
        // Container is still booting.
      }
      await new Promise(resolve => setTimeout(resolve, 250));
    }
    throw new Error("VideoDL container did not become ready on port " + CONTAINER_PORT);
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/_worker/health") {
      return Response.json({ online: true, service: "VideoDL Cloudflare", container: "managed" });
    }
    const id = env.VIDEODL_CONTAINER.idFromName("videodl-api-singleton");
    return env.VIDEODL_CONTAINER.get(id).fetch(request);
  }
};
