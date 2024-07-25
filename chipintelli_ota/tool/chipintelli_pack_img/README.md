# 项目使用

## 修改配置

根据需要修改 config/config.ini 文件


# 更新分区程序

根据需要更新对应的分区程序，其中：bootloader.bin 和 user_code.bin 是不需要更新的。其他可以更新。


# 编译运行

# 编译

g++ .\src\main.cpp .\src\inih\ini.c .\src\inih\INIReader.cpp -o pack_img

# 运行

./pack_img