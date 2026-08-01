<?php
include('global.inc');

$action = isset($_POST['action']) ? $_POST['action'] : '';
$username = isset($_POST['username']) ? trim($_POST['username']) : '';
$password = isset($_POST['password']) ? $_POST['password'] : '';
$error = '';
$notice = '';

if ($action === 'logout')
{
  $token = okj_token();
  if ($token !== '') okj_post('/local/user/logout', array('token' => $token));
  okj_forget_token();
  okj_user_refresh();
  $notice = 'Signed out.';
}
elseif ($action === 'password')
{
  $current = isset($_POST['currentPassword']) ? $_POST['currentPassword'] : '';
  $next = isset($_POST['newPassword']) ? $_POST['newPassword'] : '';
  $confirm = isset($_POST['confirmPassword']) ? $_POST['confirmPassword'] : '';

  if ($current === '' || $next === '')
  {
    $error = 'Enter your current password and a new one.';
  }
  // Checked here rather than left to the API, which is only sent one of the two
  // and so cannot tell a typo in the confirmation from the password itself.
  elseif ($next !== $confirm)
  {
    $error = 'The new passwords don\'t match.';
  }
  else
  {
    $res = okj_post('/local/user/profile/password', array(
      'token' => okj_token(),
      'currentPassword' => $current,
      'newPassword' => $next,
    ));
    if ($res['ok'])
    {
      // The API leaves the session alone on a self-service change, so the phone
      // that just did this stays signed in - only a KJ's reset ends sessions.
      $notice = 'Password changed.';
    }
    else
    {
      $error = $res['error'] !== '' ? $res['error'] : 'Could not change your password.';
    }
  }
}
elseif ($action === 'login' || $action === 'register')
{
  if ($username === '' || $password === '')
  {
    $error = 'Enter a name and a password.';
  }
  else
  {
    $path = ($action === 'register') ? '/local/user/register' : '/local/user/login';
    $res = okj_post($path, array('username' => $username, 'password' => $password));
    if ($res['ok'] && !empty($res['data']['token']))
    {
      okj_store_token($res['data']['token']);
      okj_user_refresh();
      $notice = 'Signed in as ' . (isset($res['data']['username']) ? $res['data']['username'] : $username) . '.';
    }
    else
    {
      $error = $res['error'] !== '' ? $res['error'] : 'Could not sign in.';
    }
  }
}

$user = okj_user();

siteheader();
navbar('account');

echo '<h2 class="page-title">Your account</h2>';
errorBanner($error);
if ($notice !== '') echo '<p class="notice">' . h($notice) . '</p>';

if ($user['authenticated'])
{
  echo '<p>Signed in as <strong>' . h($user['username']) . '</strong>.</p>';
  echo $user['inRotation']
    ? '<p>You&rsquo;re in tonight&rsquo;s rotation' . ($user['away'] ? ', currently marked <strong>away</strong>.' : '.') . '</p>'
    : '<p>You&rsquo;re not in the rotation yet &mdash; request a song to join it.</p>';
  echo '<p><a href="/queue.php" hx-boost="true">Manage your songs in the queue</a></p>';

  // Worth offering prominently, because staying signed in means never typing the
  // password - which is exactly how people forget it. There is no self-service
  // reset: the only way back in is the KJ setting a new one.
  echo '<form method="post" action="/account.php" class="account-form">
    <h3>Change your password</h3>
    <label>Current password</label>
    <input type="password" name="currentPassword" autocomplete="current-password" required>
    <label>New password</label>
    <input type="password" name="newPassword" autocomplete="new-password" minlength="4" required>
    <label>New password again</label>
    <input type="password" name="confirmPassword" autocomplete="new-password" minlength="4" required>
    <p class="hint">At least 4 characters. If you ever forget it, ask the KJ &mdash; they can set a
      new one for you, which signs out any phone still logged in as you.</p>
    <input type="hidden" name="action" value="password">
    <button type="submit">Change password</button>
  </form>';

  // Confirmed because this is a trapdoor: signing back in needs the password, and a
  // singer who has been signed in for months very likely no longer has it.
  echo '<form method="post" action="/account.php"
    onsubmit="return confirm(\'Sign out? You&rsquo;ll need your password to sign back in.\');">
    <input type="hidden" name="action" value="logout">
    <button type="submit" class="close">Sign out</button>
  </form>';
}
else
{
  echo '<p>Signing in is optional &mdash; you can request songs with just a name. An account lets you
    reorder or remove your own songs and mark yourself away if you step out.</p>';

  echo '<form method="post" action="/account.php" class="account-form">
    <h3>Sign in</h3>
    <label>Name</label>
    <input type="text" name="username" autocomplete="username" required>
    <label>Password</label>
    <input type="password" name="password" autocomplete="current-password" required>
    <input type="hidden" name="action" value="login">
    <button type="submit">Sign in</button>
  </form>';

  echo '<form method="post" action="/account.php" class="account-form">
    <h3>Create an account</h3>
    <label>Name</label>
    <input type="text" name="username" autocomplete="username" minlength="2" maxlength="24" required>
    <label>Password</label>
    <input type="password" name="password" autocomplete="new-password" minlength="4" required>
    <p class="hint">2&ndash;24 characters for the name, at least 4 for the password. Your name is what
      the KJ will call out, so use the one you want announced.</p>
    <input type="hidden" name="action" value="register">
    <button type="submit">Create account</button>
  </form>';
}

sitefooter();

?>
