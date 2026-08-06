#!/bin/sh

set -e

# Check if a cert and key file exist. If so, ignore. Otherwise, generate a new
# CA cert and key.
if [ -f "ca.crt" ] || [ -f "ca.key" ]; then
    echo "CA certificate or key already exists. skipping CA cert..."
else
    openssl req -x509 -newkey rsa:4096 \
        -nodes \
        -keyout ca.key \
        -out ca.crt \
        -days 3650 \
        -subj "/C=US/ST=Arizona/L=Phoenix/O=Technology/CN=papago-test-ca" \
        2>/dev/null

    openssl x509 -in ca.crt -noout -subject -dates
fi

# server certificate, signed by the CA above (instead of self-signed)
if [ -f "server.crt" ] || [ -f "server.key" ]; then
    echo "server certificate or key already exists. skipping server cert..."
else
    SAN_FILE="$(mktemp)"

    cat > "${SAN_FILE}" <<EOF
[ v3_req ]
subjectAltName = @alt_names

[ alt_names ]
DNS.1 = localhost
$(printf '%s' "${SAN_CONFIG}")
EOF

    SERVER_CSR="$(mktemp)"

    openssl req -newkey rsa:4096 \
        -nodes \
        -keyout server.key \
        -out "${SERVER_CSR}" \
        -subj "/C=US/ST=Arizona/L=Phoenix/O=Technology/CN=localhost" \
        2>/dev/null

    openssl x509 -req \
        -in "${SERVER_CSR}" \
        -CA ca.crt \
        -CAkey ca.key \
        -CAcreateserial \
        -out server.crt \
        -days 365 \
        -sha512 \
        -extfile "${SAN_FILE}" \
        -extensions v3_req 2>/dev/null

    openssl x509 -in server.crt -noout -subject -issuer -dates

    rm -f "${SAN_FILE}" "${SERVER_CSR}"
fi

# client certificate, signed by the same CA, for mutual TLS testing
if [ -f "client.crt" ] || [ -f "client.key" ]; then
    echo "client certificate or key already exists. skipping client cert..."
else
    CLIENT_CSR="$(mktemp)"

    openssl req -newkey rsa:4096 \
        -nodes \
        -keyout client.key \
        -out "${CLIENT_CSR}" \
        -subj "/C=US/ST=Arizona/L=Phoenix/O=Technology/CN=papago-client" \
        2>/dev/null

    openssl x509 -req \
        -in "${CLIENT_CSR}" \
        -CA ca.crt \
        -CAkey ca.key \
        -CAcreateserial \
        -out client.crt \
        -days 365 \
        -sha512 2>/dev/null

    openssl x509 -in client.crt -noout -subject -issuer -dates

    rm -f "${CLIENT_CSR}"
fi

rm -f ca.srl

exit 0
