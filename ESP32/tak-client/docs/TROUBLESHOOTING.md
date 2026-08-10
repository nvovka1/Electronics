# Troubleshooting

Every problem actually hit during this deployment, as symptom → cause → fix.
Most of these fail *silently* or with an error that points somewhere other than
the real cause, which is why they are worth recording.

---

## Build

### `BUILD FAILED` at `:takserver-tool-ui:bundle`

```
[eslint] eslint-config-react-app/jest#overrides[0]:
        Environment key "jest/globals" is unknown
```

Upstream ships that React app without a committed `package-lock.json`, so npm
resolves jest 30.x against the repo's pinned Node 18.12.1. Create React App then
fails the build on a lint error.

```bash
printf "\nDISABLE_ESLINT_PLUGIN = true\n" >> src/takserver-tool-ui/.env
```

The leading `\n` matters — the file has no trailing newline, so a plain append
concatenates onto the previous line. Check with `od -c .env`.

### Gradle fails immediately on version

The build derives its version from `git describe`. A ZIP download or shallow
clone has no tags. Use a full `git clone` and confirm `git describe --tags`
prints the tag before building.

---

## Startup

### `ClassNotFoundException: tak.server.ServerConfiguration`

The README's `java -jar takserver.war` is incomplete. The WAR's `Main-Class` is
`PropertiesLauncher`, which does not add `WEB-INF/classes` to the classpath.

```
JDK_JAVA_OPTIONS="-Dloader.path=WEB-INF/lib-provided,WEB-INF/lib,WEB-INF/classes …"
```

### `NullPointerException` from `Util.getTAKIgniteConfigPath()`

`TAKIgniteConfig.xml` is missing. Copy
`takserver-core/example/TAKIgniteConfig.example.xml` to `/opt/tak/`.

### `IllegalStateException: SSLContext is not initialized`

A red herring — the real error is a few lines above:

```
WARN SSLConfig - exception initializing trust store
java.io.IOException: keystore password was incorrect
Caused by: UnrecoverableKeyException: failed to decrypt safe contents entry
```

A keystore or truststore password in `CoreConfig.xml` is wrong. The keystore and
truststore use **different** passwords (see DEPLOYMENT.md §5). "safe contents"
is PKCS#12 terminology — Java 9+ keytool writes PKCS#12 even for `.jks` files.

`keytool -list` is not a sufficient test; it only checks the store integrity
password. Round-trip the store to test entry decryption:

```bash
keytool -importkeystore -srckeystore X.jks -srcstorepass "$PW" \
        -destkeystore /tmp/t.jks -deststorepass testtest123
```

### Config edits appear to do nothing

The **config service** loads `CoreConfig.xml` and distributes it over Ignite.
Restarting only `messaging` or `api` leaves them running the previous config.
Always restart `config` first, then messaging, then api.

### Duplicate port / `_name` after editing CoreConfig

The example already has active inputs and connectors. Check before adding:

```bash
grep -nE "^\s*<(input|connector)" CoreConfig.xml
```

A naive duplicate check across the whole file gives false positives — the
commented-out examples contain the same ports.

---

## Certificates

### `Algorithm (RC2-40-CBC : 0) unsupported`

TAK's `.p12` files use legacy encryption. OpenSSL 3 needs `-legacy` to read
them, and modern browsers and iOS may reject them outright. Re-export with
AES-256 (see CLIENTS.md).

### `Mac verify error: invalid password?` on a truststore

`truststore-root.p12` is encrypted with the **CA** passphrase, not the client
p12 passphrase. Build a replacement from the public `ca.pem` rather than putting
the CA passphrase into a distributable bundle.

### `ERR_BAD_SSL_CLIENT_AUTH_CERT` in the browser

The server requires a client certificate and the browser has none. Import the
`.p12` under *Your certificates*. Confirm what the server asks for:

```bash
echo | openssl s_client -connect ukrtak.duckdns.org:8443 2>&1 | grep -A2 "Acceptable client"
```

### curl returns HTTP 000 against 8443

Not a 404 — the TLS handshake failed. Either no client certificate was supplied,
or the private key is encrypted and needs `--pass`:

```bash
curl --cacert ca.pem --cert admin.pem --key admin.key --pass "$PASS" …
```

### Enrollment returns HTTP 500

```
TakException: CertificateSigning element not found in CoreConfig!
```

The `certificateSigning` block is commented out in the example config. Create an
intermediate signing CA and enable it (DEPLOYMENT.md §6).

---

## Clients

### iTAK: "The format of this configuration could not be read"

The package is in ATAK's nested format. iTAK needs a **flat** zip containing
`config.pref` (not `server.pref`) with bare filenames.

### iTAK: nothing happens at all, no error

Check whether the phone ever reached the server:

```bash
grep -aiE "8089|handshake|Set client for subscription" /opt/tak/logs/takserver-messaging.log | tail
```

No connection attempt means the package was silently discarded client-side — the
server and certificates are not involved. Use QR enrollment instead.

### Device appears then vanishes from the map

`stale` is not far enough in the future, or the clock is wrong. CoT timestamps
are absolute UTC; an ESP32 that has not synced NTP reports 1970 and every message
is stale on arrival — accepted without complaint, never displayed.

### A new ghost contact on every reboot

The client UID is not stable. Derive it from something persistent (the firmware
uses `ESP.getEfuseMac()`).

---

## Host

### SSH suddenly times out

ufw's `22/tcp LIMIT` blocks a source IP after ~6 connections in 30 seconds, and
each further attempt refreshes the block. Stop connecting for a minute. Batch
remote work into single sessions.

Confirm the host is alive with ICMP, which is not rate-limited:

```powershell
Test-Connection -ComputerName 212.43.155.64 -Count 3
```

### `Could not get lock /var/lib/dpkg/lock-frontend`

`unattended-upgrades` is applying security updates. Wait for it — never kill
dpkg mid-transaction.

### WebTAK 404s after previously working

The injected assets are gone from the WAR. Modifying the WAR while the three
JVMs hold it open did not persist:

```bash
cd /opt/tak && md5sum -c .war.md5
jar tf takserver.war | grep -c "^webtak/"      # expect 2452
```

Re-inject with the services **stopped**.

### `http://host/` or `https://host/` does not respond

TAK listens on 8089/8443/8446 and, if configured, 443. Nothing listens on port
80. `http://` will never work unless a redirect is added.

---

## Useful one-liners

```bash
# service state
systemctl is-active takserver-config takserver-messaging takserver-api

# real errors only, current run
journalctl -u takserver-messaging --since "-5min" | grep -aiE "error|exception" | grep -av "^\s*at "

# what is bound
ss -tlnp | grep java

# who is connected
sudo -u postgres psql -d cot -tAc \
  "SELECT uid, count(*), max(servertime) FROM cot_router GROUP BY uid ORDER BY 3 DESC;"

# effective sshd settings
sshd -T | grep -E "passwordauthentication|permitrootlogin"
```
