#!/bin/bash

for i in {1..8}; do
	~/jerry/minio/minio server --address :900${i} http://[::1]:9001/tmp/tenant${i} &
done
