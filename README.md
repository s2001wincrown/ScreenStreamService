# ScreenStream

ScreenStream runs as a desktop process in the signed-in user's interactive Windows session, so DXGI Desktop Duplication can capture that user's display. It is no longer started through the Windows Service Control Manager.

## Commands

Run these from an interactive user session:

- `ScreenStream.exe --install` registers the watchdog to start when the current user signs in. Administrator rights are not required.
- `ScreenStream.exe --uninstall` removes that startup entry and requests the running watchdog to stop.
- `ScreenStream.exe --start` starts the watchdog and its desktop worker.
- `ScreenStream.exe --stop` stops the watchdog and worker.
- Launching `ScreenStream.exe` without arguments starts the watchdog.

The watchdog restarts the worker if it exits or crashes, with a five-second delay between attempts. The GUI-subsystem app does not open a console at sign-in; command-line management commands attach to the calling console when available. The worker and watchdog write diagnostics to `%LOCALAPPDATA%\ScreenStream\ScreenStream.log`.

The HTTP preview page is at `http://127.0.0.1:8888/`. It refreshes finite JPEG snapshots so the browser page can finish loading. JPEG encoding is requested on demand by active preview clients and is skipped when no preview client is connected. `/stream` remains available for clients that want a continuous MJPEG response, which intentionally stays open and may show a loading indicator in a browser tab.

`Config.ini` must remain beside the executable. If a previous version was installed as a Windows service, remove the legacy `ScreenStreamSvc` from an elevated terminal so it cannot launch in Session 0:

```powershell
sc.exe stop ScreenStreamSvc
sc.exe delete ScreenStreamSvc
```

This per-user desktop app does not modify existing service registrations.
