# Build and run

Run these commands from the repository root.

## Alpine Linux

```sh
apk add --no-cache build-base curl-dev pkgconf
make -B
./bin/dk.exe examples/main.dk
```

## Ubuntu

These are the build dependencies used by the GitHub Actions workflow.

```sh
sudo apt-get update
sudo apt-get install --yes build-essential libcurl4-openssl-dev
make -B
./bin/dk.exe examples/main.dk
```

## Windows

```sh
sh setup_windows.sh
make CURL_PREFIX=third_party/curl 'CURL_LIBS=-Lthird_party/curl/lib -lcurl.dll'
bin/dk.exe examples/main.dk
```
