# TAK Server deployment runbook

How `ukrtak.duckdns.org` was built, from a bare VPS to a running server with a
browser map. Written so it can be repeated from scratch.

Every command here was actually run. Where something in the official README is
wrong or incomplete, that is called out — those gaps cost the most time.

---

## 0. Target host

| | |
|---|---|
| Provider | Zomro, "Exclusive Intel \| NL-3 v.2" |
| Spec | 4 vCPU / 10 GB RAM / 70 GB NVMe, Intel Xeon |
| IPv4 | `212.43.155.64` |
| OS | Ubuntu 22.04.5 LTS x86_64 |
| Hostname | `ukrtak.duckdns.org` (DuckDNS, free) |

**Sizing matters.** TAK Server's documented minimum is **4 cores / 8 GB RAM /
40 GB**. Three JVMs plus PostgreSQL will not fit in less, and the Gradle build
alone cannot run on a 1–2 GB box. Anything under spec fails twice — at build
time and at run time.

x86_64 is worth having: the `postgis/postgis` Docker image is amd64-only, and
ARM hosts force a native PostgreSQL build.

---

## 1. Access

Generate a key **on the machine you will administer from**, so tooling can use
it directly:

```bash
ssh-keygen -t ed25519 -C "zomro-tak" -f ~/.ssh/id_ed25519_zomro
```

Add the `.pub` via the provider panel. If the panel only applies keys at OS
install time, either reinstall (harmless on an empty box) or add the key by hand
through the panel's console:

```bash
mkdir -p /root/.ssh && echo "<your-public-key>" >> /root/.ssh/authorized_keys
chmod 700 /root/.ssh && chmod 600 /root/.ssh/authorized_keys
```

### Disable password authentication

Ubuntu cloud images enable it, and root-with-password on a public IP is scanned
constantly. **Ordering is the trap**: `sshd` takes the *first* value it sees, and
`Include /etc/ssh/sshd_config.d/*.conf` sits at line 12 — before the settings in
the main file. `50-cloud-init.conf` therefore beats both `sshd_config` and
`60-cloudimg-settings.conf`. An override must sort *earlier* than `50-`:

```bash
cat > /etc/ssh/sshd_config.d/00-hardening.conf <<'EOF'
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin prohibit-password
EOF
sshd -t && systemctl reload ssh     # validate BEFORE reloading
sshd -T | grep -E "passwordauthentication|permitrootlogin"
```

`KbdInteractiveAuthentication no` matters — without it, PAM can still allow
password login despite `PasswordAuthentication no`.

> **ufw rate-limits SSH.** The default `22/tcp LIMIT` rule blocks a source IP
> after ~6 connections in 30 seconds, and each further attempt refreshes the
> block. Batch remote work into single SSH sessions rather than many small ones.

---

## 2. Build TAK Server from source

```bash
apt update && apt install -y git openjdk-17-jdk
cd /opt
git clone https://github.com/TAK-Product-Center/Server.git takserver
cd takserver && git checkout 5.7-RELEASE-14
git describe --tags        # MUST print the tag
cd src && ./gradlew clean bootWar bootJar shadowJar
```

### Gotcha: the clone must carry tags

`src/build.gradle` derives the version from `git describe` via grgit. A ZIP
download or `--depth 1` clone fails the build
([issue #8](https://github.com/TAK-Product-Center/Server/issues/8)). Use a full
clone. (Issue #4, "missing license.gradle", is stale — the file is present.)

### Gotcha: the npm build is rotted

`:takserver-tool-ui:bundle` fails with:

```
[eslint] eslint-config-react-app/jest#overrides[0]:
        Environment key "jest/globals" is unknown
```

Upstream ships that React app **without a committed `package-lock.json`**, so a
fresh install resolves jest 30.x against the repo's pinned Node 18.12.1 (see the
`EBADENGINE` warnings). Create React App then fails the build on a *lint* error.

The module is not optional — `takserver-core/build.gradle:117` has
`bootWar.dependsOn ":takserver-tool-ui:deployToCore"`, so skipping it means no
WAR at all. Fix:

```bash
cd /opt/takserver/src/takserver-tool-ui
printf "\nDISABLE_ESLINT_PLUGIN = true\n" >> .env     # note the leading newline
```

The existing `.env` has **no trailing newline**; appending without one produces
`GENERATE_SOURCEMAP = falseDISABLE_ESLINT_PLUGIN = true` on a single line.
Verify with `od -c .env`.

Linting is a dev-time check; the emitted bundle is unchanged.

### Artifacts

```
takserver-core/build/libs/takserver-core-5.7-RELEASE-14.war       264 MB
takserver-schemamanager/build/libs/schemamanager-5.7-RELEASE-14-uber.jar
takserver-usermanager/build/libs/UserManager-5.7-RELEASE-14-all.jar
```

Note the usermanager is `UserManager-…-all.jar`, not the name the README implies.

---

## 3. Runtime layout

```bash
mkdir -p /opt/tak/logs
cp .../takserver-core-5.7-RELEASE-14.war        /opt/tak/takserver.war
cp .../schemamanager-5.7-RELEASE-14-uber.jar    /opt/tak/schemamanager.jar
cp .../UserManager-5.7-RELEASE-14-all.jar       /opt/tak/UserManager.jar
cp /opt/takserver/src/takserver-core/example/CoreConfig.example.xml      /opt/tak/CoreConfig.xml
cp /opt/takserver/src/takserver-core/example/TAKIgniteConfig.example.xml /opt/tak/TAKIgniteConfig.xml
cp -r /opt/takserver/src/takserver-core/scripts/certs /opt/tak/certs
```

**`TAKIgniteConfig.xml` is required** and the README never mentions it. Without
it, `UserManager` throws a `NullPointerException` from
`Util.getTAKIgniteConfigPath()`.

**The cert scripts are at `takserver-core/scripts/certs/`**, not the
`utils/misc/certs` the README states.

---

## 4. Database

```bash
# Ubuntu 22.04 ships PG14; TAK pins 15 — add PGDG
install -d /usr/share/postgresql-common/pgdg
curl -fsSL https://www.postgresql.org/media/keys/ACCC4CF8.asc \
  -o /usr/share/postgresql-common/pgdg/apt.postgresql.org.asc
echo "deb [signed-by=/usr/share/postgresql-common/pgdg/apt.postgresql.org.asc] https://apt.postgresql.org/pub/repos/apt jammy-pgdg main" \
  > /etc/apt/sources.list.d/pgdg.list
apt update && apt install -y postgresql-15 postgresql-15-postgis-3
```

Bind to localhost only — it must never be publicly reachable:

```bash
sed -i "s/^#\?listen_addresses.*/listen_addresses = 'localhost'/" \
  /etc/postgresql/15/main/postgresql.conf
systemctl restart postgresql@15-main
ss -tln | grep 5432          # expect 127.0.0.1:5432
```

Create the role and database, then apply the schema:

```sql
CREATE ROLE martiuser LOGIN PASSWORD '<db-password>';
CREATE DATABASE cot OWNER martiuser;
\c cot
CREATE EXTENSION postgis;
```

```bash
cd /opt/tak
java -jar schemamanager.jar -url jdbc:postgresql://127.0.0.1:5432/cot \
  -user martiuser -password '<db-password>' upgrade
```

Expect ~94 updates and ~64 tables.

> `Failure purging database 'cot'. ERROR: must be owner of extension postgis` is
> harmless. It appears because the extension was created by the `postgres`
> superuser, so `martiuser` cannot drop it. It only affects schemamanager's
> *purge* path. PostgreSQL has no `ALTER EXTENSION … OWNER TO`, so fixing it
> properly means recreating the extension as `martiuser`.

> `apt` may block on `unattended-upgrades` holding the dpkg lock. Wait for it;
> never kill dpkg mid-transaction.

---

## 5. Certificates

```bash
cd /opt/tak/certs
sed -i 's/^COUNTRY=US$/COUNTRY=UA/' cert-metadata.sh   # COUNTRY is hardcoded, not ${COUNTRY:-}
export STATE=Kyiv CITY=Kyiv ORGANIZATION=TAK ORGANIZATIONAL_UNIT=TAK
export CAPASS="$(cat /opt/tak/.capass)"    # protects the CA private key
export PASS="$(cat /opt/tak/.p12pass)"     # protects distributed .p12 bundles

./makeRootCa.sh --ca-name ukrtak-ca
./makeCert.sh server ukrtak.duckdns.org    # CN must match what clients dial
./makeCert.sh client admin
```

Keep `CAPASS` and `PASS` **separate**. The p12 password is written in plaintext
into every data package and typed into browsers, so it circulates. The CA
passphrase must not — anyone holding `ca-do-not-share.key` plus `CAPASS` can mint
certificates your server trusts.

Verify the server certificate has a **subjectAltName**; modern clients ignore CN:

```bash
openssl x509 -in files/ukrtak.duckdns.org.pem -noout -ext subjectAltName
openssl verify -CAfile files/ca.pem files/ukrtak.duckdns.org.pem
```

### Gotcha: the stores use different passwords

Determined empirically, and assuming otherwise produces an opaque startup
failure:

| Store | Password |
|---|---|
| `ukrtak.duckdns.org.jks` (keystore) | `PASS` |
| `truststore-root.jks` (truststore) | `CAPASS` |
| `fed-truststore.jks` | `CAPASS` |
| `intermediate-signing-signing.jks` | `CAPASS` |

`keytool -list` only checks the store integrity password. To test whether
entries actually decrypt, round-trip it:

```bash
keytool -importkeystore -srckeystore X.jks -srcstorepass "$PW" \
        -destkeystore /tmp/t.jks -deststorepass testtest123
```

---

## 6. CoreConfig

The shipped example already has active inputs on 8089/8090 and connectors on
8443/8444/8446. **Check before adding** — duplicating a port or `_name` breaks
startup, and a naive grep counts commented-out examples as duplicates.

Set the security block (note the two different passwords):

```xml
<tls context="TLSv1.2" keymanager="SunX509"
     keystore="JKS" keystoreFile="certs/files/ukrtak.duckdns.org.jks" keystorePass="<PASS>"
     truststore="JKS" truststoreFile="certs/files/truststore-root.jks" truststorePass="<CAPASS>">
```

The federation block has its **own** `<tls>` with a separate truststore — it also
needs `CAPASS`, or messaging dies at startup with
`IllegalStateException: SSLContext is not initialized`. The real error is above
it in the log: `exception initializing trust store / keystore password was
incorrect`.

Add a plaintext input for bring-up (remove once clients use TLS):

```xml
<input _name="streamtcp" protocol="stcp" port="8088" auth="anonymous"/>
```

Enable the browser UI on the connectors you expose:

```xml
<connector port="8443" _name="https" enableAdminUI="true" enableWebtak="true" enableNonAdminUI="true"/>
<connector port="443"  _name="https_default" enableAdminUI="true" enableWebtak="true" enableNonAdminUI="true"/>
```

### Certificate enrollment

Required for phones to onboard without data packages. The example ships it
commented out, and without it the enrollment API returns HTTP 500 with
`CertificateSigning element not found in CoreConfig!`.

```bash
cd /opt/tak/certs && ./makeCert.sh ca intermediate-signing
# produces intermediate-signing-signing.jks (note the doubled suffix)
```

```xml
<certificateSigning CA="TAKServer">
    <certificateConfig>
        <nameEntries>
            <nameEntry name="O" value="TAK"/>
            <nameEntry name="OU" value="TAK"/>
        </nameEntries>
    </certificateConfig>
    <TAKServerCAConfig keystore="JKS"
        keystoreFile="certs/files/intermediate-signing-signing.jks"
        keystorePass="<CAPASS>" validityDays="365" signatureAlg="SHA256WithRSA"
        CAkey="certs/files/intermediate-signing.key"
        CAcertificate="certs/files/intermediate-signing.pem"/>
</certificateSigning>
```

---

## 7. Services

Run as an unprivileged user — every TAK port is above 1024, so there is no
reason for a public-facing JVM to be root:

```bash
useradd -r -m -d /opt/tak -s /usr/sbin/nologin tak
chown -R tak:tak /opt/tak
chmod 600 /opt/tak/.capass /opt/tak/.p12pass
```

### Gotcha: the README's java command does not work

`java -jar takserver.war` fails with
`ClassNotFoundException: tak.server.ServerConfiguration`. The WAR's `Main-Class`
is `PropertiesLauncher`, which — unlike `WarLauncher` — does not put
`WEB-INF/classes` on the classpath. Upstream's own `setenv.sh` supplies it:

```
JDK_JAVA_OPTIONS="-Dloader.path=WEB-INF/lib-provided,WEB-INF/lib,WEB-INF/classes \
 -Dio.netty.tmpdir=/opt/tak -Djava.io.tmpdir=/opt/tak -Dio.netty.native.workdir=/opt/tak \
 -Djava.net.preferIPv4Stack=true -Djava.security.egd=file:/dev/./urandom \
 -DIGNITE_UPDATE_NOTIFIER=false -DIGNITE_QUIET=true -Djdk.tls.client.protocols=TLSv1.2"
```

Per-service settings, taken from `takserver-core/scripts/`:

| Service | Profile | Heap (10 GB host) | Extra |
|---|---|---|---|
| config | `config` | 512m | `-Dkeystore.pkcs12.legacy` |
| messaging | `messaging` | 4g | `ExecStartPre=/bin/rm -rf /opt/tak/tmp` |
| api | `api` | 2g | `-Dkeystore.pkcs12.legacy` |

All three use
`-server -XX:+AlwaysPreTouch -XX:+UseG1GC -XX:+ScavengeBeforeFullGC -XX:+DisableExplicitGC`.
Note `-Dkeystore.pkcs12.legacy` belongs on **config and api**, not just api.

Binding port 443 as `tak` needs a capability drop-in:

```ini
# /etc/systemd/system/takserver-api.service.d/bind443.conf
[Service]
AmbientCapabilities=CAP_NET_BIND_SERVICE
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
```

### Gotcha: always restart config first

The **config service loads `CoreConfig.xml` and distributes it over Ignite**.
Restarting only `messaging` or `api` leaves them consuming the *old* config —
edits appear to have no effect. Order is always:

```bash
systemctl restart takserver-config && sleep 40
systemctl restart takserver-messaging && sleep 80
systemctl restart takserver-api
```

---

## 8. Firewall

```bash
ufw allow 8089/tcp   # TLS CoT — real clients
ufw allow 8443/tcp   # admin UI + WebTAK
ufw allow 443/tcp    # same, default HTTPS port
ufw allow 8446/tcp   # certificate enrollment
ufw allow 8088/tcp   # plaintext CoT — bring-up ONLY, unauthenticated
```

Port 8088 accepts spoofed contacts from anyone who finds it. Restrict it to your
own source IP, or close it once clients use TLS. Do **not** open 8444/9000/9001
unless you actually federate.

---

## 9. WebTAK

**WebTAK is not in the open-source repo.** The server code contains the routing
(`ApiConfiguration.java:432` redirects `/webtak` → `/webtak/index.html`, and
`ROLE_WEBTAK` is handled) but the web app itself ships only in the official
tak.gov release. No configuration will make a source build serve a map.

Download `takserver-docker-5.7-RELEASE-<n>.zip` from tak.gov and inject the
static assets. Note tak.gov ships newer builds than the public GitHub tag —
RELEASE-43 (Spring Boot 3.5.13) versus the source's RELEASE-14 (3.4.5). Swapping
the whole WAR would mean a different server against your existing schema;
injecting the ~12 MB of static assets keeps the tested build intact.

```bash
# extract webtak/ from the official WAR, then, ON THE SERVER:
systemctl stop takserver-api takserver-messaging takserver-config   # REQUIRED
cd /tmp && jar xf webtak.zip
jar uf /opt/tak/takserver.war webtak
md5sum /opt/tak/takserver.war > /opt/tak/.war.md5
chown tak:tak /opt/tak/takserver.war
# then start config -> messaging -> api
```

**Stop the services first.** Injecting while the three JVMs hold the WAR open
did not persist — after later restarts the file was byte-identical to the
original again, with the injected version gone. Verify afterwards with
`md5sum -c /opt/tak/.war.md5`.

Result: `https://ukrtak.duckdns.org/webtak/index.html`, requiring a client
certificate.

---

## 10. Verification

```bash
ss -tlnp | grep -E '443|8088|8089|8443|8446'         # all bound
Test-NetConnection ukrtak.duckdns.org -Port 8089      # from Windows
echo | openssl s_client -connect ukrtak.duckdns.org:8089 2>/dev/null \
  | openssl x509 -noout -subject                      # correct cert served
```

Authenticated API check (note `--pass`, the key is encrypted):

```bash
curl --cacert certs/files/ca.pem --cert certs/files/admin.pem \
     --key certs/files/admin.key --pass "$PASS" \
     https://ukrtak.duckdns.org:8443/Marti/api/version
```

End-to-end proof without any hardware — send a CoT event to 8088 and confirm it
lands:

```sql
SELECT uid, count(*), max(servertime) FROM cot_router GROUP BY uid;
```

---

## Files on the server

| Path | Purpose |
|---|---|
| `/opt/takserver` | source checkout |
| `/opt/tak` | runtime: WAR, config, certs, logs |
| `/opt/tak/CoreConfig.xml` | main configuration |
| `/opt/tak/certs/files/` | CA, server and client certificates |
| `/opt/tak/.capass` | CA passphrase (root only) |
| `/opt/tak/.p12pass` | p12 / keystore passphrase (root only) |
| `/opt/tak/.enrolluser` | enrollment account password (root only) |
| `/opt/tak/add-device.sh` | creates and registers a device |
| `/opt/tak/devices/` | generated client bundles |
| `/opt/tak/takserver.war.orig` | pre-WebTAK backup (rollback) |
