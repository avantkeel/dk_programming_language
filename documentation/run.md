# commands to buld and run the project in windows system
```sh
sh setup_windows.sh
make CURL_PREFIX=third_party/curl 'CURL_LIBS=-Lthird_party/curl/lib -lcurl.dll'
bin/dk.exe examples/main.dk
```
