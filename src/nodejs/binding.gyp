 {
   "target_defaults": {
     "include_dirs": [ "../.." ],
     "libraries": [ "-L ../../.libs -liot-uv -liot-app -liot-common" ]
   },

   "targets": [
     {
       "target_name": "iot-appfw",
       "sources": [ "src/appfw.cpp" ]
     }
   ]
 }
