# WebView2 SDK

Microsoft.Web.WebView2 1.0.4191.47, obtained from the official NuGet package:
https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/1.0.4191.47/microsoft.web.webview2.1.0.4191.47.nupkg

Package SHA-256: `f492bbf547d0da329553b6727435b677579b1e9f91cc9e4a1ad029366d5f23d0`.

Only the native header, Release x64 static loader, license, and notices are retained.
The browser runtime itself is not bundled. Screen sharing requires an installed
Microsoft Edge WebView2 Runtime with AV1 WebRTC support. Chat, voice, and files
do not load the browser runtime unless a screen-share window is opened.

The build embeds ScreenShare.html in oi.exe and copies the SDK license and
notice files into bin. No remote webpage supplies the screen-share UI.
