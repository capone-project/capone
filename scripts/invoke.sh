#!/bin/sh

set -e

cd build

CLIENT_CFG=../config/client.conf
CLIENT_KEY=$(cat ${CLIENT_CFG} | sed -n 's/^public_key=\(.*\)$/\1/p')

INVOKER_CFG=../config/server.conf
INVOKER_KEY=$(cat ${INVOKER_CFG} | sed -n 's/^public_key=\(.*\)$/\1/p')
INVOKER_ADDR=127.0.0.1
INVOKER_PORT=1239

SERVICE_CFG=../config/server.conf
SERVICE_KEY=$(cat ${SERVICE_CFG} | sed -n 's/^public_key=\(.*\)$/\1/p')
SERVICE_TYPE=exec
SERVICE_ADDR=127.0.0.1
SERVICE_PORT=1237
SERVICE_ARGS="command=ls
              arg=-l"

SERVICE_SESSION=$(./cpn-client request \
    ${CLIENT_CFG} \
    ${INVOKER_KEY} \
    ${SERVICE_KEY} \
    ${SERVICE_ADDR} \
    ${SERVICE_PORT} \
    ${SERVICE_ARGS})
SERVICE_ID="$(echo "$SERVICE_SESSION" | awk 'NR == 1 { print $2 }')"
SERVICE_SECRET="$(echo "$SERVICE_SESSION" | awk 'NR == 2 { print $2 }')"

INVOKE_SESSION=$(./cpn-client request \
    ${CLIENT_CFG} \
    ${CLIENT_KEY} \
    ${INVOKER_KEY} \
    ${INVOKER_ADDR} \
    ${INVOKER_PORT} \
    service-identity=${SERVICE_KEY} \
    service-address=${SERVICE_ADDR} \
    service-port=${SERVICE_PORT} \
    service-type=${SERVICE_TYPE} \
    sessionid=${SERVICE_ID} \
    secret=${SERVICE_SECRET})
INVOKE_ID="$(echo "$INVOKE_SESSION" | awk 'NR == 1 { print $2 }')"
INVOKE_SECRET="$(echo "$INVOKE_SESSION" | awk 'NR == 2 { print $2 }')"

./cpn-client connect \
    ${CLIENT_CFG} \
    ${INVOKER_KEY} \
    ${INVOKER_ADDR} \
    ${INVOKER_PORT} \
    invoke \
    ${INVOKE_ID} \
    ${INVOKE_SECRET}
