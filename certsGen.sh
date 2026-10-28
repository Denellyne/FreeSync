#!/bin/bash

mkdir -p certs
cd certs || echo "Couldn't cd into certs directory" && exit
openssl genrsa -out priv.pem 2048
openssl rsa -in priv.pem -outform PEM -pubout -out pub.pem
