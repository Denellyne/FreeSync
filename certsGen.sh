#!/bin/bash

mkdir -p certs
cd certs 
openssl genrsa -out priv.pem 2048
openssl rsa -in priv.pem -outform PEM -pubout -out pub.pem
