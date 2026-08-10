# TAK Server + ESP32 client

A self-hosted TAK Server and an ESP32 that reports its position to it.

Live at **`ukrtak.duckdns.org`** (212.43.155.64) — TAK Server 5.7-RELEASE-14 built
from source on Ubuntu 22.04.

| | |
|---|---|
| Browser map | `https://ukrtak.duckdns.org/webtak/index.html` |
| TLS CoT (clients) | `ukrtak.duckdns.org:8089` |
| Plaintext CoT (bring-up) | `ukrtak.duckdns.org:8088` |
| Certificate enrollment | `ukrtak.duckdns.org:8446` |

Deeper reference: [DEPLOYMENT.md](docs/DEPLOYMENT.md) ·
[CLIENTS.md](docs/CLIENTS.md) · [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)

---

# Part 1 — Host the server

From a bare Ubuntu 22.04 x86_64 VPS. **Minimum 4 cores / 8 GB RAM / 40 GB** —
below that the build itself cannot run.

### 1.1 Get in and lock the door

On your own machine:

```bash
ssh-keygen -t ed25519 -C "tak" -f ~/.ssh/id_ed25519_tak
```

Add `~/.ssh/id_ed25519_tak.pub` to the provider panel, then on the server:

```bash
cat > /etc/ssh/sshd_config.d/00-hardening.conf <<'EOF'
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin prohibit-password
EOF
sshd -t && systemctl reload ssh
sshd -T | grep -E "passwordauthentication|permitrootlogin"
```

> The filename must sort **before** `50-cloud-init.conf` — sshd takes the first
> value it sees, and the `Include` sits at the top of `sshd_config`.

### 1.2 Build TAK Server from source

```bash
apt update && apt install -y git openjdk-17-jdk
cd /opt
git clone https://github.com/TAK-Product-Center/Server.git takserver
cd takserver && git checkout 5.7-RELEASE-14
git describe --tags        # must print the tag, or the build fails
```

Work around the rotted npm build (upstream ships no `package-lock.json`):

```bash
printf "\nDISABLE_ESLINT_PLUGIN = true\n" >> src/takserver-tool-ui/.env
```

Build:

```bash
cd /opt/takserver/src
./gradlew clean bootWar bootJar shadowJar        # ~10 min
```

### 1.3 Lay out the runtime

```bash
mkdir -p /opt/tak/logs
cd /opt/takserver/src
cp takserver-core/build/libs/takserver-core-*.war        /opt/tak/takserver.war
cp takserver-schemamanager/build/libs/schemamanager-*.jar /opt/tak/schemamanager.jar
cp takserver-usermanager/build/libs/UserManager-*.jar     /opt/tak/UserManager.jar
cp takserver-core/example/CoreConfig.example.xml          /opt/tak/CoreConfig.xml
cp takserver-core/example/TAKIgniteConfig.example.xml     /opt/tak/TAKIgniteConfig.xml
cp -r takserver-core/scripts/certs                        /opt/tak/certs
```

> `TAKIgniteConfig.xml` is mandatory and undocumented. The cert scripts are in
> `takserver-core/scripts/certs`, not where the README says.

### 1.4 Database

```bash
install -d /usr/share/postgresql-common/pgdg
curl -fsSL https://www.postgresql.org/media/keys/ACCC4CF8.asc \
  -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc
echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] https://apt.postgresql.org/pub/repos/apt jammy-pgdg main" \
  > /etc/apt/sources.list.d/pgdg.list
apt update && apt install -y postgresql-15 postgresql-15-postgis-3

sed -i "s/^#\?listen_addresses.*/listen_addresses = 'localhost'/" \
  /etc/postgresql/15/main/postgresql.conf
systemctl restart postgresql@15-main
```

```bash
sudo -u postgres psql -c "CREATE ROLE martiuser LOGIN PASSWORD 'e815f795745e';"
sudo -u postgres createdb -O martiuser cot
sudo -u postgres psql -d cot -c "CREATE EXTENSION postgis;"

cd /opt/tak
java -jar schemamanager.jar -url jdbc:postgresql://127.0.0.1:5432/cot \
  -user martiuser -password 'e815f795745e' upgrade
```

### 1.5 Certificates

```bash
cd /opt/tak
umask 077
openssl rand -hex 24 > .capass      # protects the CA private key
openssl rand -hex 16 > .p12pass     # protects distributed .p12 bundles

cd certs
sed -i 's/^COUNTRY=US$/COUNTRY=UA/' cert-metadata.sh
export STATE=Kyiv CITY=Kyiv ORGANIZATION=TAK ORGANIZATIONAL_UNIT=TAK
export CAPASS="$(cat /opt/tak/.capass)" PASS="$(cat /opt/tak/.p12pass)"

./makeRootCa.sh --ca-name ukrtak-ca
./makeCert.sh server ukrtak.duckdns.org      # CN MUST match the hostname clients dial
./makeCert.sh client admin
./makeCert.sh ca intermediate-signing        # needed for enrollment
```

Keep the two passphrases separate: the p12 password ends up in data packages and
browsers, the CA passphrase must never leave the server.

### 1.6 CoreConfig

Edit `/opt/tak/CoreConfig.xml`. The keystore and truststore use **different**
passwords:

```xml
<tls context="TLSv1.2" keymanager="SunX509"
     keystore="JKS" keystoreFile="certs/files/ukrtak.duckdns.org.jks" keystorePass="<P12PASS>"
     truststore="JKS" truststoreFile="certs/files/truststore-root.jks" truststorePass="<CAPASS>">
```

The federation block has its own `<tls>` — its truststore also needs `<CAPASS>`,
or messaging will not start.

Add the bring-up input and enable the browser UI:

```xml
<input _name="streamtcp" protocol="stcp" port="8088" auth="anonymous"/>
<connector port="8443" _name="https" enableAdminUI="true" enableWebtak="true" enableNonAdminUI="true"/>
<connector port="443"  _name="https_default" enableAdminUI="true" enableWebtak="true" enableNonAdminUI="true"/>
```

Enable certificate enrollment (without this, phones cannot enroll):

```xml
<certificateSigning CA="TAKServer">
    <certificateConfig>
        <nameEntries/>
    </certificateConfig>
    <TAKServerCAConfig keystore="JKS"
        keystoreFile="certs/files/intermediate-signing-signing.jks"
        keystorePass="<CAPASS>" validityDays="365" signatureAlg="SHA256WithRSA"/>
</certificateSigning>
```

> `<nameEntries/>` is deliberately empty. Each entry listed here becomes an RDN
> the client's CSR **must** carry; if the count does not match exactly, signing
> fails with `CSR validation failed!` and the phone reports a connection error.

### 1.7 Services

```bash
useradd -r -m -d /opt/tak -s /usr/sbin/nologin tak
chown -R tak:tak /opt/tak && chmod 600 /opt/tak/.capass /opt/tak/.p12pass
```

Create three units in `/etc/systemd/system/` — `takserver-config`,
`takserver-messaging`, `takserver-api`. All share:

```ini
[Service]
User=tak
WorkingDirectory=/opt/tak
Environment="JDK_JAVA_OPTIONS=-Dloader.path=WEB-INF/lib-provided,WEB-INF/lib,WEB-INF/classes -Dio.netty.tmpdir=/opt/tak -Djava.io.tmpdir=/opt/tak -Dio.netty.native.workdir=/opt/tak -Djava.net.preferIPv4Stack=true -Djava.security.egd=file:/dev/./urandom -DIGNITE_UPDATE_NOTIFIER=false -DIGNITE_QUIET=true -Djdk.tls.client.protocols=TLSv1.2"
ExecStart=/usr/bin/java -server -XX:+UseG1GC -Xmx<HEAP> -Dspring.profiles.active=<PROFILE> -jar /opt/tak/takserver.war
```

| Service | Profile | Heap | Extra |
|---|---|---|---|
| config | `config` | 512m | `-Dkeystore.pkcs12.legacy` |
| messaging | `messaging` | 4g | `ExecStartPre=/bin/rm -rf /opt/tak/tmp` |
| api | `api` | 2g | `-Dkeystore.pkcs12.legacy` |

> `-Dloader.path=…` is **required**. Without it the WAR fails with
> `ClassNotFoundException: tak.server.ServerConfiguration` — the README's plain
> `java -jar takserver.war` does not work.

Start **in order**, always config first — it distributes config to the others:

```bash
systemctl daemon-reload
systemctl enable --now takserver-config    && sleep 40
systemctl enable --now takserver-messaging && sleep 80
systemctl enable --now takserver-api       && sleep 110
systemctl is-active takserver-config takserver-messaging takserver-api
```

### 1.8 Firewall

```bash
ufw allow 8089/tcp   # TLS CoT
ufw allow 8443/tcp   # admin UI + WebTAK
ufw allow 443/tcp    # same on the default HTTPS port
ufw allow 8446/tcp   # certificate enrollment
ufw allow 8088/tcp   # plaintext CoT - UNAUTHENTICATED, close when done
```

### 1.9 Browser map (optional)

WebTAK is **not** in the open-source repo. Download
`takserver-docker-5.7-RELEASE-<n>.zip` from [tak.gov](https://tak.gov), extract
`webtak/` from its `takserver.war`, then:

```bash
systemctl stop takserver-api takserver-messaging takserver-config   # REQUIRED
cd /tmp && jar xf webtak.zip && jar uf /opt/tak/takserver.war webtak
md5sum /opt/tak/takserver.war > /opt/tak/.war.md5
chown tak:tak /opt/tak/takserver.war
# start config -> messaging -> api
```

> Stop the services first. Injecting while the JVMs hold the WAR open does not
> persist.

### 1.10 Verify

```bash
ss -tlnp | grep -E '443|8088|8089|8443|8446'
echo | openssl s_client -connect ukrtak.duckdns.org:8089 2>/dev/null | openssl x509 -noout -subject
```

---

# Part 2 — Register a new user with a certificate

### The one-command way

From this project:

```bash
./tools/new-device.sh <cert-name> [callsign]
```

Example — an ESP32 identity called `esp32-scout1` shown on the map as `SCOUT-1`:

```bash
./tools/new-device.sh esp32-scout1 SCOUT-1
```

It generates the certificate, registers it as a standard user, downloads
`include/certs.h`, and sets `TAK_CALLSIGN`.

On the server directly, for any client type:

```bash
/opt/tak/add-device.sh <name> <atak|itak|esp32>
scp root@212.43.155.64:/opt/tak/devices/<file> .
```

### The manual way

```bash
cd /opt/tak/certs
export STATE=Kyiv CITY=Kyiv ORGANIZATION=TAK ORGANIZATIONAL_UNIT=TAK
export CAPASS="$(cat /opt/tak/.capass)" PASS="$(cat /opt/tak/.p12pass)"

./makeCert.sh client <name>

cd /opt/tak
sudo -u tak java -jar UserManager.jar certmod certs/files/<name>.pem
```

> **Never add `-A`.** It means *administrator*. Re-running `certmod` without it
> does not clear the flag — you must delete and re-register:
> ```bash
> java -jar UserManager.jar usermod -D <name>
> java -jar UserManager.jar certmod certs/files/<name>.pem
> ```

Check who holds what:

```bash
grep -oE '<User identifier="[^"]+"|role="[^"]+"' /opt/tak/UserAuthenticationFile.xml
```

Only `admin` should be `ROLE_ADMIN`.

### A username/password account (for enrollment)

```bash
sudo -u tak java -jar UserManager.jar usermod -p '<password>' <username>
```

Password policy: at least 15 characters with an uppercase, a lowercase, a digit
and a special character.

---

# Part 3 — Connect a phone

## Option A — QR enrollment (recommended)

No files to transfer. Scan this in iTAK:

<img src="docs/img/itak-quickconnect.png" width="240" alt="iTAK quick-connect QR">

It encodes iTAK's quick-connect format — `description,host,port,protocol`:

```
ukrtak,ukrtak.duckdns.org,8089,ssl
```

iTAK then prompts for credentials and requests its own certificate:

- **Username:** `takuser`
- **Password:** `ssh root@212.43.155.64 "cat /opt/tak/.enrolluser"`

Regenerate the QR for a different server:

```bash
python -c "import qrcode; qrcode.make('ukrtak,ukrtak.duckdns.org,8089,ssl').save('qr.png')"
```

> Requires the `certificateSigning` block from §1.6. Without it enrollment
> returns HTTP 500 and the phone shows a generic connection error.

## Option B — data package

```bash
/opt/tak/add-device.sh phone-1 itak     # or: atak
```

The two formats are **not** interchangeable:

| | ATAK | iTAK |
|---|---|---|
| Pref file | `server.pref` | `config.pref` |
| Layout | `MANIFEST/` + `cert/` | flat, no folders |

Import in iTAK under **Settings → Network → Upload Server Package**.

---

# Part 4 — The ESP32 firmware

```bash
cp include/secrets.example.h include/secrets.h   # set WIFI_SSID, WIFI_PASS
pio run -e takclient -t upload -t monitor
```

Serial should show WiFi join, an NTP-synced UTC time (**not 1970**), then
`TX PLI #1`, `#2`… every 5 seconds.

Switch from bring-up to TLS once it works:

- `include/config.h` — uncomment `#define TAK_USE_TLS 1`
- `include/secrets.h` — `TAK_PORT` → `8089`

### There is no registration message

CoT has no registration handshake. The server creates the contact from the `uid`
and `callsign` in the **first position report**, and keeps it alive only while
fresh reports arrive before each message's `stale` time. The send-on-a-timer loop
in `main.cpp` *is* the registration.

### Traps already handled in the firmware

- **Clock** — CoT timestamps are absolute UTC. Without NTP the board reports
  1970 and every message is stale on arrival: accepted silently, never displayed.
- **Stable UID** — derived from `ESP.getEfuseMac()`. A random UID spawns a fresh
  ghost contact on every reboot.
- **GPIO 16** — on this PICO-D4 it is the internal flash CS. Driving it as OLED
  reset bricks boot into a silent `TG1WDT_SYS_RESET` loop.

### Layout

| Path | Purpose |
|---|---|
| `include/config.h` | identity, position, cadence, transport selection |
| `include/secrets.h` | WiFi and server details (gitignored) |
| `include/certs.h` | CA + client PEMs for TLS (gitignored) |
| `src/CotEvent.*` | CoT XML, UTC timestamps, device UID |
| `src/StcpTransport.*` / `src/TlsTransport.*` | plaintext TCP / mutual TLS |
| `src/main.cpp` | WiFi, NTP, OLED status, report loop, backoff |
| `tools/new-device.sh` | create an identity and wire it in |

Position comes from `getPosition()` in `main.cpp` — currently fixed coordinates
walking a 25 m circle. Swapping in a real GPS means replacing that function body;
nothing else touches raw position.
