
I’ll review the existing firmware and frontend architecture, then check Firebase’s current free-tier limits and security options before outlining a deployment plan.

Read
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\README.md"
}

Read
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\web\\src\\api.js"
}

Read
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\web\\package.json"
}

Read
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\src\\web.h"
}

Read
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\src\\wifi.h"
}

Searched the web for `Firebase Spark free plan Realtime Database limits simultaneous connections st...`
Completed with input: {
  "query": "Firebase Spark free plan Realtime Database limits simultaneous connections stored data download hosting authentication 2026 ESP32 REST authentication security rules identity toolkit pricing"
}

Searched the web for `Firebase Spark plan Cloud Firestore free quota reads writes deletes storage h...`
Completed with input: {
  "query": "Firebase Spark plan Cloud Firestore free quota reads writes deletes storage hosting bandwidth cloud functions billing required ESP32 REST api authentication"
}

Ran terminal command: git status --short; Get-Content web\vite.config.js; Get-Content tools\build_web.py

Search for `^#{1,3} |API|HTTPS|TLS|history|sample|seconds|time`

Search for `api\(|/api/|setInterval|setTimeout|usePolling|usePoll`

Search for `uri|/api/|https|auth|history|status`

Read
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\web\\src\\App.jsx"
}

Read
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\web\\src\\hooks.js"
}

Read
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\src\\app.h"
}

Fetched [https://firebase.google.com/pricing](https://firebase.google.com/pricing)
Completed with input: {"url":"https://firebase.google.com/pricing"}

Fetched [https://firebase.google.com/docs/database/usage/limits](https://firebase.google.com/docs/database/usage/limits)
Completed with input: {
  "url": "https://firebase.google.com/docs/database/usage/limits",
  "max_length": 13000
}

Fetched [https://firebase.google.com/docs/database/rest/auth](https://firebase.google.com/docs/database/rest/auth)
Completed with input: {
  "url": "https://firebase.google.com/docs/database/rest/auth",
  "max_length": 11000
}

Fetched [https://firebase.google.com/docs/hosting/usage-quotas-pricing](https://firebase.google.com/docs/hosting/usage-quotas-pricing)
Completed with input: {
  "url": "https://firebase.google.com/docs/hosting/usage-quotas-pricing",
  "max_length": 9000
}

The existing app already separates a local HTTPS API from a Preact frontend, so the plan can reuse the interface without exposing the device through the home firewall. I’m checking the exact data and quota implications before recommending a sync model.

Read , lines 179 to 234
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\README.md",
  "view_range": [
    179,
    234
  ]
}

Read , lines 1 to 220
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\web\\src\\Dashboard.jsx",
  "view_range": [
    1,
    220
  ]
}

Read , lines 110 to 195
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\src\\web.cpp",
  "view_range": [
    110,
    195
  ]
}

Read
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\src\\main.cpp"
}

Search for `Realtime Database|Downloads|GB/month|Spark|Authentication|Cloud Functions|GB ...`

Search for `ID token|access token|refresh|secret|auth=|Bearer`

Fetched [https://firebase.google.com/docs/reference/rest/auth](https://firebase.google.com/docs/reference/rest/auth)
Completed with input: {"url":"https://firebase.google.com/docs/reference/rest/auth"}

Fetched [https://firebase.google.com/docs/database/security](https://firebase.google.com/docs/database/security)
Completed with input: {
  "url": "https://firebase.google.com/docs/database/security",
  "max_length": 6500
}

Fetched [https://firebase.google.com/docs/projects/billing/firebase-pricing-plans](https://firebase.google.com/docs/projects/billing/firebase-pricing-plans)
Completed with input: {
  "url": "https://firebase.google.com/docs/projects/billing/firebase-pricing-plans",
  "max_length": 11000
}

Read , lines 25 to 100
Completed with input: {
  "path": "C:\\Users\\wwtor\\AppData\\Local\\Temp\\1794108185603-copilot-tool-output-a43b11ecafb5455f9e25febb625df13d.txt",
  "view_range": [
    25,
    100
  ]
}

Read , lines 185 to 247
Completed with input: {
  "path": "C:\\Data\\code\\water-pump-monitor\\web\\src\\Dashboard.jsx",
  "view_range": [
    185,
    247
  ]
}

Search for `signInWithPassword|securetoken.googleapis.com|refresh_token|expires_in`

Fetched [https://firebase.google.com/docs/database/usage/billing](https://firebase.google.com/docs/database/usage/billing)
Completed with input: {"url":"https://firebase.google.com/docs/database/usage/billing"}

Fetched [https://firebase.google.com/docs/auth/web/password-auth](https://firebase.google.com/docs/auth/web/password-auth)
Completed with input: {
  "url": "https://firebase.google.com/docs/auth/web/password-auth",
  "max_length": 6000
}

# Firebase deployment plan

## Recommendation

Use **Firebase Hosting, Firebase Authentication, and Realtime Database on the Spark plan**. The ESP32 publishes a limited, read-only view of its data through outbound HTTPS; a Firebase-hosted version of the existing UI reads that view. The local app and pump controller remain independent of Firebase.

```text
Home network                                      Firebase
┌──────────────────────┐    outbound HTTPS    ┌────────────────────┐
│ ESP32-S3             │ ───────────────────▶ │ Realtime Database  │
│ Pump control +       │                      └─────────┬──────────┘
│ local HTTPS app      │                                │ read only
└──────────┬───────────┘                      ┌─────────▼──────────┐
           │ local HTTPS                      │ Hosted Preact UI   │
    Local browser                             │ + Firebase Auth    │
                                              └────────────────────┘
```

No inbound firewall rule, port forwarding, or direct cloud-to-device command channel is needed.

## 1. Define the first release

The first cloud release should support:

- Current pump state, capacity estimate, counters, fault/back-off state, and last update time.
- The existing capacity chart, daily history, and event log, subject to the device’s existing bounded history.
- **Monitoring only:** no pump control, settings changes, capacity corrections, log clearing, credential changes, certificate changes, or firmware uploads.
- A clear **“Cloud monitoring — read only”** label and an explicit stale/offline warning.

The ESP32 remains the source of truth. Cloud data is a replicated view, not a control dependency or a guaranteed event archive.

## 2. Connect the ESP32 to Firebase

### Device identity and transport

1. Create a **separate Firebase Authentication account for the device**, distinct from the homeowner’s account. Provision its strong credential locally, for example through a serial setup flow—not in source code or a firmware image.
2. Use Firebase’s [Authentication REST API](https://firebase.google.com/docs/reference/rest/auth) to sign in and refresh short-lived ID tokens. Use the ID token with the [Realtime Database REST API](https://firebase.google.com/docs/database/rest/auth).
3. Store credentials and refresh tokens securely in device storage, avoid logging them or authenticated URLs, and provide a local reprovisioning/revocation procedure. Evaluate ESP32 flash encryption and secure boot for physical-device protection.
4. Validate Firebase’s TLS certificate and hostname using the ESP-IDF trust store; do not disable certificate checks. Wait for a usable clock where TLS requires it.
5. Never place a service-account private key or database admin secret on the device. Treat the Firebase web API key as project configuration, **not** as an authorization secret.

### Publishing behavior

- Add a low-priority network task, separate from the existing 100 ms control loop in `main.cpp`. Copy a small snapshot while holding the existing app mutex, then release it **before** serialization or network I/O.
- Start with a **60-second heartbeat**, plus prompt, debounced publishing on meaningful changes such as pump start/stop, fault, or back-off. Make the period configurable and enforce a minimum interval to prevent a noisy input from flooding Firebase.
- Publish a bounded snapshot of the data needed by the cloud UI: status, permitted display settings, daily history, capacity history, and recent events. The app already retains up to 90 days of daily data, 256 capacity samples, and 100 events; reuse those bounds rather than create an unbounded cloud log.
- Include a schema version, device identifier, device sample time when available, and a **server-assigned receipt timestamp**. The receipt time lets the UI detect stale data even if the device clock is wrong.
- On Wi-Fi, DNS, authentication, TLS, or quota failure: log and expose the error locally, retry with exponential backoff and jitter, and continue local operation. After reconnect, upload the newest snapshot. Events that roll out of the local bounded log during a long outage may be lost from the cloud view; do not imply otherwise.
- Make cloud connectivity optional and ensure that Firebase availability is **not** added to firmware OTA health checks or pump safety decisions.

A single bounded snapshot is a simple starting point. Measure its actual serialized size; if history dominates traffic, split frequently changing status from less frequently changing history and subscribe to those paths separately.

## 3. Reuse the frontend safely

Today, `api.js` sends Basic-auth requests to the local device, while `Dashboard.jsx` polls status every five seconds and exposes control actions. Those calls cannot simply be pointed at Firebase.

Refactor around a common data interface with two implementations:

|                | Local build                        | Cloud build                                |
| -------------- | ---------------------------------- | ------------------------------------------ |
| Authentication | Existing device login              | Firebase Authentication                    |
| Data source    | Existing `/api` endpoints        | Authorized Realtime Database subscriptions |
| Capabilities   | Existing local read/write behavior | Explicitly read-only                       |
| Hosting        | Embedded in firmware               | Firebase Hosting                           |

- Share presentation components and normalized data shapes; adapt Firebase snapshots to those shapes. Use subscriptions rather than copying the five-second polling schedule to Firebase.
- Gate **all** write surfaces, not just the pump tile: menu actions, settings, capacity refresh/add, log clear, password, certificate, and OTA. The cloud data adapter should also reject writes, and Firebase rules must deny writes by human viewer accounts.
- Deliberately decide which Network and Hardware fields are useful remotely. Avoid publishing SSID, local IP/MAC addresses, usernames, or other sensitive configuration by default.
- Display “last received,” loading, permission error, and stale states. A reasonable initial stale threshold is **three heartbeat intervals**; a disconnected browser should also show that it cannot confirm current status. Do not display default “enabled” or “off” states as if they were live when no data exists.
- Keep the two builds separate. `vite.config.js` currently writes to `data/`, and `build_web.py` embeds a fixed set of assets. A cloud build should use a different output directory and must not overwrite those local assets; verify local asset generation still works if code splitting is introduced.

## 4. Set up security and initial deployment

1. Create a Firebase project on **Spark**. Enable Realtime Database and the chosen Firebase Authentication providers; use Firebase Hosting for static files.
2. Create the device identity and homeowner identity separately. Protect the homeowner’s sign-in and Firebase/Google project-admin accounts with strong authentication and MFA where supported. Disable unused sign-in providers and avoid adding public sign-up to the UI.
3. Deploy **deny-by-default database rules**:
   - Only the explicitly authorized homeowner UID can read the device’s monitoring paths.
   - Only the device UID can write its assigned paths; the homeowner cannot write them.
   - Validate the allowed structure and basic value types and bounds. Do not grant broad access at a parent path: Realtime Database permissions cascade.
   - A future smart app gets its own path and identity/permissions rather than inheriting pump access.
4. Test rules in the Firebase Emulator Suite with unauthenticated, unauthorized, viewer, device, and wrong-device identities. Test denial of every viewer write and attempts to write outside the assigned device path.
5. Deploy rules first, then the cloud frontend with the Firebase CLI. Provision the device credential locally and verify its first authorized publish.
6. Verify from outside the home network that the homeowner can sign in and see data, while anonymous or other accounts cannot. Confirm the local app still works with the internet disconnected.

The Firebase-hosted HTML and JavaScript are public static assets; **the data is protected by Authentication and database rules**, not by hiding the site.

## 5. Keep usage within the free offering

Realtime Database is a good fit here because its Spark allowance is based primarily on **stored data and downloaded bytes**, rather than a per-document read/write quota. The published Spark limits are **1 GB stored, 10 GB/month downloaded, and 100 simultaneous connections**. Hosting has its own separate storage and transfer quotas. Cloud Functions are not available on Spark, so this plan does not rely on them. See [Firebase pricing](https://firebase.google.com/pricing) and [Realtime Database billing](https://firebase.google.com/docs/database/usage/billing).

**Illustrative sizing—not a guarantee:** if a subscribed status payload averages 2 KB and changes once per minute, one browser open continuously receives roughly:

```text
2 KB × 60 × 24 × 30 = 86.4 MB/month
```

Two continuously open browsers would receive roughly 173 MB, before protocol overhead, reconnects, event-driven updates, and history downloads. If the **entire** snapshot is retransmitted on every update, use its full measured size in that calculation instead. Device uploads and writes are also workload and service-limit considerations, even though Realtime Database Spark is not priced by a Firestore-style write count.

- Set internal targets well below the limits, such as **under 25% of download allowance** during normal operation.
- Measure actual payloads and console usage after a week, including reconnects and multiple open tabs. Monitor database storage, downloads, connections, and Hosting usage as more home apps are added.
- Firebase’s [pricing table](https://firebase.google.com/pricing) currently lists Hosting transfer as **360 MB/day**, while its [Hosting quota documentation](https://firebase.google.com/docs/hosting/usage-quotas-pricing) describes **10 GB/month**. Verify the limit shown for the actual project before launch and plan conservatively around the lower rate.
- Stay on Spark if avoiding charges is essential. Quota exhaustion can interrupt cloud service; the local controller and UI must remain usable. Switching to Blaze introduces potential charges, and budget alerts are not spending caps.

## 6. Acceptance checks

Before relying on it, test:

- Pump operation and local UI with Firebase disabled, internet down, and cloud authentication revoked.
- Fault/start/stop updates, reconnection, reboot, invalid clock, failed TLS validation, and stale-data display.
- All authorization rules and all cloud UI write controls, including direct requests bypassing the UI.
- Local and cloud builds independently, plus firmware size, free heap during TLS, and responsiveness of the control loop under repeated cloud failures.
- A week of measured usage against the projected budget.

**Suggested implementation order:** rules and data contract → isolated device publisher → read-only frontend adapter and separate build → deployment and failure testing → usage tuning.
