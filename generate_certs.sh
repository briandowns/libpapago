#!/bin/sh

set -e

if [ -f "server.crt" ] || [ -f "server.key" ]; then
    echo "certificate or key already exists. exiting..."
    exit 0
fi

CONFIG_FILE="$(mktemp)"

cat > "${CONFIG_FILE}" <<EOF
[ req ]
default_bits       = 4096
prompt             = no
default_md         = sha512
distinguished_name = dn
req_extensions     = req_ext

[ dn ]
CN = localhost

[ req_ext ]
subjectAltName = @alt_names

[ alt_names ]
DNS.1 = localhost
$(printf "${SAN_CONFIG}")
EOF

openssl req -x509 -newkey rsa:4096 -nodes \
    -keyout server.key \
    -out server.crt \
    -days 365 \
    -subj "/C=US/ST=Arizona/L=Phoenix/O=Technology/CN=localhost" \
    -config "${CONFIG_FILE}" \
    2>/dev/null

openssl x509 -in server.crt -noout -subject -dates

rm -f "${CONFIG_FILE}"

exit 0
