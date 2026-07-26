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
opened by the phone, not by the webserver**, so it has to be an address the *browser* can reach — the
LAN address of the machine running OpenKJ, e.g. `http://192.168.1.20:5050`. `$openkjApiBase` usually
can't be reused for it, because that one is resolved server-side and is often `127.0.0.1` or a
Docker-internal name, neither of which means anything on a phone.

Left empty (the default), the rotation just re-polls every `$queuePollSeconds`. With it set, the poll
drops to the much slower `$queueStreamPollSeconds` and stays on only as a safety net. An OpenKJ too
old to have the stream route answers 404, which shuts the subscriber down for good, so those builds
quietly fall back to that poll on their own.

### Configuring OpenKJ

You need a build of OpenKJ with Local Mode (the `gtachibana/OpenKJ` fork). Set the app mode to **Local
Mode** and make sure the embedded API is enabled — it listens on port 5050 bound to all interfaces by
default.

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
