#!/bin/bash

mkdir -p ~/.local/share/freesync/certs
cd ~/.local/share/freesync/certs 
openssl genrsa -out priv.pem 2048
openssl rsa -in priv.pem -outform PEM -pubout -out pub.pem
