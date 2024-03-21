#!/bin/bash

# 确保脚本以root权限运行
if [ "$(id -u)" != "0" ]; then
   echo "此脚本需要root权限，请使用sudo运行。" 1>&2
   exit 1
fi

# 复制iotMonitor到/usr/bin目录，并确保iotMonitor具有可执行权限
cp iotMonitor /usr/bin/iotmonitor
chmod +x /usr/bin/iotmonitor

# 复制libpaho-mqtt库到/usr/local/lib目录
cp ./lib/libpaho-mqtt* /usr/local/lib/
# ln -s /usr/local/lib/libpaho-mqtt3as.so.1.3.13  /usr/local/lib/libpaho-mqtt3as.so.1
# ln -s /usr/local/lib/libpaho-mqtt3as.so.1 /usr/local/lib/libpaho-mqtt3as.so

# ln -s /usr/local/lib/libpaho-mqtt3cs.so.1.3.13  /usr/local/lib/libpaho-mqtt3cs.so.1
# ln -s /usr/local/lib/libpaho-mqtt3cs.so.1 /usr/local/lib/libpaho-mqtt3cs.so

ln -s /usr/local/lib/libpaho-mqtt3c.so.1.3.13  /usr/local/lib/libpaho-mqtt3c.so.1
ln -s /usr/local/lib/libpaho-mqtt3c.so.1 /usr/local/lib/libpaho-mqtt3c.so

# ln -s /usr/local/lib/libpaho-mqtt3a.so.1.3.13  /usr/local/lib/libpaho-mqtt3a.so.1
# ln -s /usr/local/lib/libpaho-mqtt3a.so.1 /usr/local/lib/libpaho-mqtt3a.so

# 复制libwiringPi库到/lib目录
cp ./lib/libwiringPi* /lib/

ln -s /lib/libwiringPiDev.so.2.52 /lib/libwiringPiDev.so
ln -s /lib/libwiringPi.so.2.52 /lib/libwiringPi.so

# 确保复制操作成功
if [ $? -eq 0 ]; then
    echo "文件复制成功。"
else
    echo "文件复制失败，请检查路径和权限。"
    exit 1
fi

# 完成脚本执行
exit 0