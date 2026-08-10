# Connecting clients

Four kinds of client can reach `ukrtak.duckdns.org`, and they need genuinely
different things. That difference is the source of most of the wasted time on
this deployment, so it is spelled out here.

| Client | Transport | Needs |
|---|---|---|
| ESP32 firmware | 8088 plaintext, or 8089 TLS | `certs.h` with a **decrypted** key |
| iTAK (iOS) | 8089 TLS | **flat** zip, `config.pref` — or QR enrollment |
| ATAK (Android) | 8089 TLS | **nested** zip, `MANIFEST/` + `server.pref` |
| Browser (WebTAK) | 443 / 8443 | `.p12` imported, re-encrypted with AES |

---

## Identity vs callsign

Two different things, easy to conflate:

- **Certificate CN** — the identity the server *authenticates*. Must be unique
  across the server; appears in `UserAuthenticationFile.xml`.
- **Callsign** — the label other operators see on the map. Cosmetic.

The firmware sets its callsign in `TAK_CALLSIGN`; the certificate CN comes from
whatever name the certificate was issued to. They do not have to match.

---

## Adding a device

From this project:

```bash
./tools/new-device.sh <cert-name> [callsign]
```

That generates the certificate on the server, registers it as a standard user,
downloads `include/certs.h`, and sets `TAK_CALLSIGN`. It validates the things
that otherwise fail silently — invalid characters in the CN, XML-breaking
characters in the callsign, a truncated download, an encrypted private key.

Or directly on the server, for any client type:

```bash
/opt/tak/add-device.sh <name> <atak|itak|esp32>
# output lands in /opt/tak/devices/
```

### Never use `certmod -A`

`-A` means **administrator**. Devices must be registered without it:

```bash
java -jar UserManager.jar certmod certs/files/<name>.pem      # correct
java -jar UserManager.jar certmod -A certs/files/<name>.pem   # makes an ADMIN
```

Re-running `certmod` without `-A` does **not** clear the flag. To remove it,
delete the entry and re-register:

```bash
java -jar UserManager.jar usermod -D <name>
java -jar UserManager.jar certmod certs/files/<name>.pem
```

Check who holds what:

```bash
grep -oP '<User identifier="\K[^"]+|role="\K[^"]+' /opt/tak/UserAuthenticationFile.xml
```

Only `admin` should show `ROLE_ADMIN`.

---

## ESP32

`certs.h` needs three PEM blocks: the CA, the client certificate, and the
private key **decrypted**. mbedTLS cannot read an encrypted key, and TAK's
`makeCert.sh` produces `-----BEGIN ENCRYPTED PRIVATE KEY-----`:

```bash
openssl rsa -in <name>.key -passin pass:"$PASS" -out <name>.key.pem
```

Switch the firmware from bring-up to TLS:

- `include/config.h`: uncomment `#define TAK_USE_TLS 1`
- `include/secrets.h`: `TAK_PORT` → `8089`

Budget ~45 KB heap for the TLS session. Measured flash use goes from 61 % to
72 %.

---

## iTAK (iOS)

iTAK's package format is **not** ATAK's. Getting this wrong produces
*"The format of this configuration could not be read"*:

| | ATAK | iTAK |
|---|---|---|
| Pref file | `server.pref` | **`config.pref`** |
| Layout | `MANIFEST/` + `cert/` folders | **flat, no folders** |
| Cert paths in prefs | `cert/foo.p12` | bare filename |

A valid iTAK zip contains exactly three entries at the root:

```
config.pref
truststore-root.p12
<client>.p12
```

### The truststore password trap

`truststore-root.p12` is encrypted with the **CA** passphrase, not the client
one — `makeRootCa.sh` and `makeCert.sh` use different secrets. A package that
tells the client to open it with the p12 password fails with
`Mac verify error: invalid password?`.

Do **not** put the CA passphrase in a data package. Build a fresh truststore
from the public CA certificate instead — it holds no private key:

```bash
openssl pkcs12 -export -nokeys -in ca.pem -out truststore-root.p12 \
  -name ukrtak-ca -passout pass:"$PASS" -certpbe AES-256-CBC -macalg sha256
```

### Enrollment instead of packages

Simpler, and avoids package formats entirely. Scan a QR containing iTAK's
quick-connect CSV:

```
ukrtak,ukrtak.duckdns.org,8089,ssl        # description,host,port,protocol
```

iTAK then prompts for credentials and requests its own certificate. Requires the
`certificateSigning` block in CoreConfig (see DEPLOYMENT.md §6) — without it the
enrollment API returns HTTP 500.

- Username: `takuser`
- Password: `cat /opt/tak/.enrolluser` on the server

---

## Browser (WebTAK)

`https://ukrtak.duckdns.org/webtak/index.html`

Requires a client certificate. Without one Chrome reports
`ERR_BAD_SSL_CLIENT_AUTH_CERT` and curl fails the handshake entirely (HTTP 000,
not a 4xx).

### Re-encrypt the .p12 first

TAK's p12 files use **RC2-40-CBC**, which OpenSSL 3 refuses outright and modern
browsers may reject:

```
Algorithm (RC2-40-CBC : 0) unsupported
```

Convert to AES before importing:

```bash
openssl pkcs12 -legacy -in admin.p12 -passin pass:"$PW" -nodes -out /tmp/plain.pem
openssl pkcs12 -export -in /tmp/plain.pem -out admin-modern.p12 -passout pass:"$PW" \
  -keypbe AES-256-CBC -certpbe AES-256-CBC -macalg sha256
rm /tmp/plain.pem
```

For a **cert-only** store (no private key) add `-nokeys` — the `-nodes`
round-trip above produces nothing to export and fails.

Then in Chrome → Manage certificates:

1. **Your certificates** → import `admin-modern.p12`
2. **Authorities** → import `ca.pem`, trust for identifying websites
3. Restart Chrome

---

## Checking who is connected

```bash
curl --cacert ca.pem --cert admin.pem --key admin.key --pass "$PASS" \
  https://ukrtak.duckdns.org:8443/Marti/api/clientEndPoints
```

Or straight from the database:

```sql
SELECT uid, count(*) AS reports, max(servertime) AS last
FROM cot_router GROUP BY uid ORDER BY last DESC;
```

The messaging log records each client as it attaches:

```
DistributedSubscriptionManager - Set client for subscription: tcp:3 to ESP32-1 (ESP32-1811B3F4E9D4)
```
