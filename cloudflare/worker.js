import { DurableObject } from "cloudflare:workers";

const CONTAINER_PORT = 10000;
const INACTIVITY_TIMEOUT_MS = 5 * 60 * 1000;

export class VideoDLContainer extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.starting = undefined;
    this.monitoring = undefined;

    if (ctx.container?.running) {
      void ctx.blockConcurrencyWhile(() =>
        ctx.container.setInactivityTimeout(INACTIVITY_TIMEOUT_MS)
      );
      this.observeExit();
    }
  }

  async fetch(request) {
    this.starting ??= this.startAndWaitForPort().finally(() => {
      this.starting = undefined;
    });
    await this.starting;

    const url = new URL(request.url);
    url.protocol = "http:";
    url.host = "container";

    const forwarded = new Request(url, request);
    forwarded.headers.delete("host");

    // Overwrite, do not trust, caller-supplied forwarding headers.
    const clientIp = request.headers.get("CF-Connecting-IP");
    forwarded.headers.delete("X-Forwarded-For");
    if (clientIp) forwarded.headers.set("X-Forwarded-For", clientIp);

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

    this.observeExit();
    await container.setInactivityTimeout(INACTIVITY_TIMEOUT_MS);

    const port = container.getTcpPort(CONTAINER_PORT);
    for (let attempt = 0; attempt < 100; attempt++) {
      try {
        const response = await port.fetch("http://container/api/health", {
          signal: AbortSignal.timeout(1000)
        });
        await response.body?.cancel();
        if (response.ok) return;
      } catch {
        // The API process may still be starting.
      }
      await scheduler.wait(200);
    }
    throw new Error("VideoDL C++ container did not become ready on port " + CONTAINER_PORT);
  }

  observeExit() {
    if (this.monitoring) return;
    this.monitoring = this.ctx.container.monitor()
      .then(() => console.log("VideoDL container exited"))
      .catch((error) => console.error("VideoDL container failed", error))
      .finally(() => { this.monitoring = undefined; });
    this.ctx.waitUntil(this.monitoring);
  }
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (url.pathname === "/_worker/health") {
      return Response.json({ ok: true, service: "videodl-worker" });
    }

    if (url.pathname.startsWith("/api/")) {
      const id = env.VIDEODL_CONTAINER.idFromName("videodl-singleton");
      return env.VIDEODL_CONTAINER.get(id).fetch(request);
    }

    return env.ASSETS.fetch(request);
  }
};
