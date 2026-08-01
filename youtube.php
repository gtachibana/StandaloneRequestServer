<?php
include('global.inc');
include('youtube-search.inc');

// The tab is gated on OpenKJ's capability, so a direct hit on this URL after the
// KJ switched the feature off - or while yt-dlp is broken - lands here rather
// than on a search box whose every result would be refused.
if (!okj_youtube_enabled())
{
  if (!okj_is_fragment())
  {
    siteheader();
    navbar('youtube');
  }
  echo '<div id="yt-target"><p class="notice">YouTube requests aren&rsquo;t switched on right now.
    Try <a href="/" hx-boost="true">the songbook</a>.</p></div>';
  if (!okj_is_fragment()) sitefooter();
  die();
}

$input_query = trim(preg_replace('!\s+!', ' ', isset($_GET['q']) ? $_GET['q'] : ''));

$fragment = okj_is_fragment();

if (!$fragment)
{
  siteheader();
  navbar('youtube');

  // Its own box rather than searchform(): that one posts to /search.php on a
  // 250ms keystroke trigger, which is right for a local library and wrong for a
  // search that leaves the building. This one waits for a pause or the Enter
  // key, and it is the reason there is no cost to nobody using the tab.
  echo '<div id="song-search-input-container"><input
      id="yt-search-input"
      type="search"
      name="q"
      value="' . h($input_query) . '"
      autocomplete="off"
      placeholder="Search YouTube for a karaoke track"
      hx-get="/youtube.php"
      hx-sync="this:replace"
      hx-push-url="true"
      hx-trigger="input[this.value.trim().length > 2] changed delay:700ms, search"
      hx-target="#yt-target"
      hx-select="#yt-target"
      hx-indicator="#yt-indicator"
      >
      </div>
      <div id="yt-indicator" class="htmx-indicator">Searching YouTube&hellip;</div>';
}

echo '<div id="yt-target">';

$user = okj_user();
$accepting = okj_accepting();

if (!$user['authenticated'])
{
  echo '<p class="notice">You can look now, but requesting a video needs an account &mdash;
    <a href="/account.php" hx-boost="true">sign in or create one</a>.</p>';
}

if (strlen($input_query) < 3)
{
  echo '<details open>
    <summary>Not in the songbook?</summary>
    <p>Search YouTube for a karaoke version and it will be downloaded for your turn.</p>
    <p>&ldquo;karaoke&rdquo; is added to your search automatically, so just type the song
      &mdash; and tap a thumbnail to hear it before you request it.</p>
    <p>The songbook is still the faster bet: those tracks are ready to sing right now.
      <a href="/" hx-boost="true">Search the songbook</a>.</p>
  </details>';
  if (!$accepting)
    echo '<details open>
      <summary>Requests closed</summary>
      <p>You will not be able to request songs at this time.</p>
    </details>';
}
else
{
  $res = okj_yt_search($input_query);

  if (!$res['ok'])
  {
    errorBanner($res['error']);
    echo '<p class="hint">Try again in a moment, or
      <a href="/" hx-boost="true">search the songbook</a> instead.</p>';
  }
  else
  {
    $videos = $res['videos'];
    $count = count($videos);

    if (!$count)
    {
      echo '<p><strong>Nothing on YouTube for &ldquo;' . h($input_query) . '&rdquo;</strong></p>
        <p>Try a different spelling, or add the artist&rsquo;s name.</p>';
    }
    else
    {
      $maxDuration = okj_youtube_max_duration();

      $ids = array();
      foreach ($videos as $video) $ids[] = $video['videoId'];
      $cached = okj_youtube_cached_status($ids);

      $results_str = ($count === 1) ? 'result' : 'results';
      echo '<p><strong>' . $count . ' YouTube ' . $results_str . ' for &ldquo;' . h($input_query) . '&rdquo;</strong>';
      echo $accepting
        ? '<br/>Tap the picture to preview, or the title to request it.</p>'
        : '<br/>Requests are closed, so these can only be previewed.</p>';

      echo '<div class="yt-results' . ($accepting ? '' : ' not-accepting') . '">';
      foreach ($videos as $video)
      {
        $videoId = $video['videoId'];
        $tooLong = ($maxDuration > 0 && $video['durationSeconds'] > $maxDuration);
        $label = '<span class="song-label">' . h($video['title']) . '</span>'
          . okj_song_length($video['durationSeconds']);

        echo '<div class="yt-result">';

        // The preview. Nothing is embedded until it is tapped - fifteen iframes
        // on a phone over a venue's wifi is a page that never settles - and the
        // thumbnail standing in for one is a plain <img> off YouTube's CDN.
        // youtube-nocookie.com rather than youtube.com because a singer checking
        // a track shouldn't have it charged to their watch history.
        echo '<div class="yt-media" data-video="' . h($videoId) . '">';
        if ($video['thumb'] !== '')
        {
          echo '<button type="button" class="yt-thumb" title="Preview this video"
            hx-on:click="
              document.querySelectorAll(\'.yt-media.playing\').forEach(function (m) {
                m.classList.remove(\'playing\');
                const playing = m.querySelector(\'iframe\');
                if (playing) playing.remove();
              });
              const media = this.parentElement;
              const frame = document.createElement(\'iframe\');
              frame.src = \'https://www.youtube-nocookie.com/embed/' . h($videoId) . '?autoplay=1&amp;rel=0&amp;playsinline=1\';
              frame.title = \'YouTube preview\';
              frame.allow = \'autoplay; encrypted-media; picture-in-picture\';
              frame.allowFullscreen = true;
              media.appendChild(frame);
              media.classList.add(\'playing\');
            ">
            <img src="' . h($video['thumb']) . '" alt="" loading="lazy" referrerpolicy="no-referrer">
            <span class="yt-play" aria-hidden="true">&#9654;</span>
          </button>';
        }
        echo '</div>';

        echo '<div class="yt-body">';

        if ($tooLong)
        {
          // OpenKJ would refuse this on arrival; refusing it here saves the round
          // trip and, more to the point, explains why.
          echo '<button class="result song" disabled>' . $label . '</button>';
        }
        elseif (!$accepting)
        {
          echo '<button class="result song">' . $label . '</button>';
        }
        else
        {
          // Same pattern as songButtons(): everything the request needs rides in
          // hx-vals, because neither YouTube nor OpenKJ offers a look-this-up-
          // again route once the results page is gone.
          $vals = hxvals(array(
            'videoid' => $videoId,
            // The channel stands in for the artist. It is not the recording
            // artist, but it is the truthful answer to "where did this come
            // from", which is what the KJ needs to see in the rotation.
            'artist' => $video['channel'],
            'title' => $video['title'],
            'duration' => $video['durationSeconds'],
          ));
          echo '<button
            class="result song"
            hx-post="/req-modal.php"
            hx-target="body"
            hx-swap="beforeend"
            hx-vals=\'' . $vals . '\'>' . $label . '</button>';
        }

        echo '<p class="yt-meta">';
        if ($video['channel'] !== '') echo '<span class="yt-channel">' . h($video['channel']) . '</span>';
        if ($tooLong)
          echo ' <span class="yt-badge too-long">Too long &mdash; over ' . h(okj_fmt_duration($maxDuration)) . '</span>';
        elseif (isset($cached['ready'][$videoId]))
          echo ' <span class="yt-badge ready">Ready to sing</span>';
        elseif (isset($cached['pending'][$videoId]))
          echo ' <span class="yt-badge pending">Downloading</span>';
        echo '</p>';

        echo '</div></div>';
      }
      echo '</div>';

      echo '<p class="hint">These are singers&rsquo; own picks off YouTube, not tracks your KJ
        checked &mdash; preview one before you request it.</p>';
    }
  }
}

echo '</div>';

if (!$fragment) sitefooter();

?>
