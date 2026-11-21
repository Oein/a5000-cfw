/** Widget extension that renders the camera-session Live Activity (ActivityKit). */
/** @type {import('@bacons/apple-targets/app.plugin').Config} */
module.exports = {
  type: 'widget',
  name: 'A5kLiveActivity',
  displayName: 'a5k Remote',
  bundleIdentifier: '.liveactivity',
  deploymentTarget: '16.4',
  frameworks: ['SwiftUI', 'WidgetKit', 'ActivityKit'],
  colors: {
    $accent: '#f39800',
    $widgetBackground: '#0f0f10',
  },
};
