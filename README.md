# OpenKJ Standalone Request Server

Standalone basic single-venue request server implementation for use with [OpenKJ](https://openkj.org/).

This began as a fork of the freely-provided *(thank you!)* original [OpenKJ StandaloneRequestServer](https://github.com/OpenKJ/StandaloneRequestServer), last updated Dec 21, 2018 (when I forked it in Jan 2023).

## What's new

This is now a front end for **OpenKJ's Local Mode embedded API** rather than a standalone server with
its own copy of the song library. OpenKJ owns the library, the rotation and the queue; this app just
renders them and posts back. That's what makes the singer-facing features below possible at all.

Enhancements & changes:

- Single page for active search & song requests via modal
- Search available at all times, even when requests are closed
- **Browse the songbook by artist or title, A–Z**, for when you don't know what to search for
- **Live rotation view** — who's singing now, who's up next, what was played recently
- **Turn estimates** — every singer's first row says how many singers away they are and roughly how
  long that is, and signed-in singers get a "you're up next" banner off their earliest song
- **Optional push updates** — the rotation can subscribe to OpenKJ's event stream and redraw when
  something actually moves, instead of on a timer (see `$openkjEventStreamBase` below)
- **Cheer the singer on** — tap 👏 🔥 💖 ⭐ 🎉 under whoever's at the mic and the emoji floats up
  OpenKJ's singer screen, with a running tally the singer can see from the stage. No account needed;
  the buttons appear only when the KJ has cheers switched on, and nobody is offered one for their own
  performance
- **Optional accounts.** Sign in and your requests are filed under your name, which lets you:
  - reorder your own pending songs
  - remove a song you changed your mind about
  - mark yourself **away** if you step outside, so the rotation skips you instead of stalling
  - star songs as **favorites** — tap the ☆ beside any search or browse result, and the Favorites
    page has them ready to request again in two taps
- Requesting without an account still works exactly as before — just type a name
- Bounded result sets, so a 100k+ song library can't produce a page too big to load
- [new.css](https://newcss.net/) for a lightweight css base, dark mode enforced
- [htmx](https://htmx.org/) for active search & request modal
- Fonts changed to my preferences, with a little extra CSS & JS as needed

## Usage

I'll repeat what the original README said:

> This is intended for people who already know how to configure and manage their own webservers and have a general familiarity with php. The easier and more feature rich option is to use the hosted service available at [okjsongbook.com](https://okjsongbook.com)

### Running the songbook

Requirements:

- php with the curl extension (it falls back to `allow_url_fopen` if curl is missing)
- you can use php's built in web server, or a web server with php support caddy, nginx, apache
- **network access from the webserver to the machine running OpenKJ.** This app makes outbound HTTP
  calls to OpenKJ, so the two need to be on the same machine or the same LAN. There is no database to
  configure — OpenKJ is the database.
- `settings.inc` should be edited to point `$openkjApiBase` at that OpenKJ instance, e.g.
  `http://192.168.1.20:5050`
- You probably also want to change the `$venueName` in `settings.inc` to personalize your instance,
  though OpenKJ's own event name wins if one is set

Optionally, set `$openkjEventStreamBase` to turn on push updates for the rotation view. **This one is
opened by the phone, not by the webserver**, so it has to resolve to something the *browser* can
reach. `$openkjApiBase` usually can't be reused for it, because that one is resolved server-side and
is often `127.0.0.1` or a Docker-internal name, neither of which means anything on a phone.

Two ways to satisfy that:

- `'/'` — **same-origin**, the default. The stream is fetched from whatever host served the page, so
  it arrives over the same proxy or tunnel as everything else and port 5050 is never published. Needs
  a proxy rule routing `/local/events` to the API; see [Exposing the stream](#exposing-the-stream).
- `'http://192.168.1.20:5050'` — the LAN address of the machine running OpenKJ. Simplest, but it
  means every phone talks to port 5050 directly, so **only do this on a LAN you aren't tunneling out
  of** (see the warning below).

Left empty, the rotation just re-polls every `$queuePollSeconds`. With it set, the poll drops to the
much slower `$queueStreamPollSeconds` and stays on only as a safety net — the stream redraws the
rotation the moment anything moves, and the poll is what keeps the turn estimates counting down in
between. An OpenKJ too old to have the stream route answers 404, which shuts the subscriber down for
good, so those builds quietly fall back to that poll on their own — as does a same-origin setup whose
proxy rule isn't in place yet, so **add the rule before flipping the setting** or you'll land on the
60s poll instead of the 10s one.

### Exposing the stream

> [!WARNING]
> **Never publish port 5050 to the internet.** Current OpenKJ builds require an admin session
> token for the routes that mutate show state, but everything else on that surface is
> unauthenticated by design, and builds predating those gates leave the rotation controls
> (`adminAction`, `deleteRequest`, and `clearRequests` — which drops the entire pending queue) open
> to anyone who can reach the port. The API is built to sit behind this app on a trusted LAN. Route
> around it and you've removed the lock.

The safe shape is to publish exactly one path and nothing else. With
[Cloudflare Tunnel](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/),
ingress rules match on hostname *and* path, first match wins:

```yaml
ingress:
  - hostname: songbook.example.com
    path: ^/local/events$
    service: http://127.0.0.1:5050      # the API, this one route only
  - hostname: songbook.example.com
    service: http://127.0.0.1:8080      # the PHP app, everything else
  - service: http_status:404
```

If your tunnel is token-managed from the Zero Trust dashboard rather than a `config.yml`, the same
thing is two Public Hostname entries under the tunnel, ordered path-first. Behind nginx it's a
`location = /local/events` block with `proxy_buffering off;` and `proxy_read_timeout` above 30s.

That done, `$openkjEventStreamBase = '/'` and port 5050 stays on the LAN.

Worth adding on top:

- **Rate-limit the path.** Each subscriber holds a socket that is deliberately exempt from the API's
  idle sweep, and the server caps out at 256 connections, so a few hundred `EventSource` opens will
  lock every phone in the room out. Every new connection also runs a queue query on OpenKJ's Qt main
  thread, so connect-churn shows up as the KJ's UI stuttering. A rule of ~5 requests/minute per IP on
  `/local/events` covers both; Cloudflare's free tier includes enough for this one.
- **Verify it actually streams.** `curl -N 'https://songbook.example.com/local/events?deltas=1'`
  should print a `queue` frame immediately, then something every 20s — a `tick` frame while a show is
  running, or a bare `: keepalive` comment while nothing is moving. (`?deltas=1` is what the phones
  ask for; without it every one of those 20s beats is a full snapshot instead.) If it hangs and then
  dumps everything at once, something in the chain is buffering.
- **Check the idle timeout.** Cloudflare drops a proxied connection that goes quiet for ~100s. The
  API writes something every 20s no matter how quiet the room is, which is what keeps the stream
  alive — if you put something else in front, keep its read timeout above 20s.

### Rate limits and client addresses

OpenKJ rate-limits two things per client: cheer taps, and failed logins (five in a row locks that
client out for five minutes). "Per client" means per address the API was called from — and this app
calls it server-side for everybody, so out of the box the whole venue counts as one client. A room
mid-ovation throttles itself, and one person mistyping their password locks out every singer and the
KJ.

The fix is a chain of forwarded addresses, and each hop only believes the one in front of it if it
was told to:

- **This app → OpenKJ.** Every call carries `X-Forwarded-For`. OpenKJ reads it when the webserver is
  loopback to it, which is the usual single-machine setup and needs no configuration. If the
  webserver is a different box, add its address (comma separated, if more than one) to
  `embeddedApiTrustedProxies` under `[localMode]` in OpenKJ's `openkj.ini` — in the app data
  directory on Windows and macOS, `~/.config/OpenKJ/OpenKJ.conf` on Linux. There's no field for it in
  the settings dialog, deliberately.
- **Proxy → this app.** `$trustedProxies` in `settings.inc` lists proxies whose `CF-Connecting-IP` or
  `X-Forwarded-For` this app will believe. Loopback is always believed, so a Cloudflare Tunnel
  connector or nginx on the same machine already works; the setting is for one that reaches the
  webserver over the network.

Leave them unset and you get the old behaviour, which is a nuisance and not a hole. Set them wrong —
naming a hop that doesn't overwrite the header, or one anybody can reach — and the lockout stops
meaning anything, since an attacker can just claim a fresh address per guess.

### Configuring OpenKJ

You need a build of OpenKJ with Local Mode (the `gtachibana/OpenKJ` fork). Set the app mode to **Local
Mode** and make sure the embedded API is enabled — it listens on port 5050 bound to all interfaces by
default.

**Narrow that bind address.** Tools > Settings > Local Mode has a bind address field (it defaults to
`0.0.0.0`, every interface). Given the API is unauthenticated, set it to the narrowest address that
still works for your layout:

- `127.0.0.1` if the webserver, the tunnel connector and OpenKJ are all on the one machine — which is
  the case whenever `$openkjApiBase` is already `127.0.0.1`. Nothing off-box can reach the API at
  all, including anyone on the venue's guest wifi.
- the machine's LAN address, if the webserver runs on a different box, or if phones connect straight
  to `:5050` for the event stream.

Docker complicates the first one: a container reaching the host can't use `127.0.0.1`, so a
containerized webserver needs the LAN address (or `host.docker.internal` plus a bind the container
can actually route to).

Nothing needs to be configured on the OpenKJ side to point *at* this app: unlike the original
StandaloneRequestServer, OpenKJ doesn't push anything here, so the Server URL and API key under
Tools > Settings > Network are irrelevant.

### A note on trust

The embedded API has no authentication on its library, queue and admin routes, so don't expose OpenKJ's
port 5050 to the internet. This app deliberately calls it server-side, so phones only ever talk to the
webserver — treat that as the boundary and keep both on the venue LAN.

## Development

### Docker

A docker compose file is provided for development: `docker compose up` serves the repo with php:8.0-apache at <http://localhost:8080>, with the project root bind-mounted to `/var/www/html`.

Inside the container `127.0.0.1` is the container itself, not your machine, so the default
`$openkjApiBase` won't find OpenKJ. Point it at the host instead:

```php
$openkjApiBase = 'http://host.docker.internal:5050';
```

Since `settings.inc` is tracked in git, your local API base & venue name will show up as an unstaged
change; try not to sweep it into unrelated commits.

### Checking your work

There's no test suite. To syntax-check everything without installing php locally:

```bash
docker run --rm -v "$PWD:/app" -w /app php:8.0-cli sh -c 'for f in *.php *.inc; do php -l "$f"; done'
```

### Assets (css)

Because I like needlessly optimizing things, the css is bundled & minified from `src/` to `css/style.css` with [Lightning CSS](https://lightningcss.dev/). The built `css/style.css` is committed, so you only need the node tooling if you want to change the styles.

There's no JS build — htmx comes from a CDN, and the small bits of behavior are written inline as `hx-on:*` attributes in the PHP-generated markup.

To make style changes, edit the files in `src/`:

- `src/style.css`: the entry point, which just imports [new.css](https://newcss.net/) and `_venuestyle.css`
- `src/_venuestyle.css`: the venue styles — colors, fonts, and component styling. This is the one you probably want.

Then do an `npm i` in the project root to install the required node dependencies (the new.css import resolves out of `node_modules`), and run any of these:

- `npm run watch`: watches `src/` for changes, rebuilding `css/style.css` as needed
- `npm run dev`: does a one-time development build of `css/style.css`, unminified & with a sourcemap
- `npm run build`: does a one-time production build of `css/style.css`, minified
- `npm run clean`: deletes the contents of `css/` (`build` does this first)

Remember to commit the rebuilt `css/style.css` along with your `src/` changes.
