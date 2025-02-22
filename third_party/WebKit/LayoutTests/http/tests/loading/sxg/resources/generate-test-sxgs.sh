#!/bin/sh

# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

for cmd in gen-signedexchange gen-certurl; do
    if ! command -v $cmd > /dev/null 2>&1; then
        echo "$cmd is not installed. Please run:"
        echo "  go get -u github.com/WICG/webpackage/go/signedexchange/cmd/..."
        exit 1
    fi
done

certs_dir=../../../../../../../blink/tools/blinkpy/third_party/wpt/certs
tmpdir=$(mktemp -d)

# Make dummy OCSP data for cbor certificate chains.
echo -n OCSP >$tmpdir/ocsp

# Generate the certificate chain of "127.0.0.1.sxg.pem".
gen-certurl  \
  -pem $certs_dir/127.0.0.1.sxg.pem \
  -ocsp $tmpdir/ocsp \
  > 127.0.0.1.sxg.pem.cbor

# Generate the signed exchange file.
gen-signedexchange \
  -uri https://127.0.0.1:8443/test.html \
  -status 200 \
  -content sxg-location.html \
  -certificate $certs_dir/127.0.0.1.sxg.pem \
  -certUrl https://127.0.0.1:8443/loading/sxg/resources/127.0.0.1.sxg.pem.cbor \
  -validityUrl https://127.0.0.1:8443/loading/sxg/resources/resource.validity.msg \
  -privateKey $certs_dir/127.0.0.1.sxg.key \
  -date 2018-04-01T00:00:00Z \
  -expire 168h \
  -o sxg-location.sxg \
  -miRecordSize 100

# Generate the signed exchange file which certificate file is not available.
gen-signedexchange \
  -uri https://127.0.0.1:8443/not_found_cert.html \
  -status 200 \
  -content sxg-location.html \
  -certificate $certs_dir/127.0.0.1.sxg.pem \
  -certUrl https://127.0.0.1:8443/loading/sxg/resources/not_found_cert.pem.cbor \
  -validityUrl https://127.0.0.1:8443/loading/sxg/resources/not_found_cert.validity.msg \
  -privateKey $certs_dir/127.0.0.1.sxg.key \
  -date 2018-04-01T00:00:00Z \
  -expire 168h \
  -o sxg-cert-not-found.sxg \
  -miRecordSize 100

# Generate the signed exchange file which validity URL is different origin from
# request URL.
gen-signedexchange \
  -uri https://127.0.0.1:8443/test.html \
  -status 200 \
  -content sxg-location.html \
  -certificate $certs_dir/127.0.0.1.sxg.pem \
  -certUrl https://127.0.0.1:8443/loading/sxg/resources/127.0.0.1.sxg.pem.cbor \
  -validityUrl https://www2.127.0.0.1/loading/sxg/resources/resource.validity.msg \
  -privateKey $certs_dir/127.0.0.1.sxg.key \
  -date 2018-04-01T00:00:00Z \
  -expire 168h \
  -o sxg-invalid-validity-url.sxg \
  -miRecordSize 100

rm -fr $tmpdir
