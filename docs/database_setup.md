# 数据库设置指南

## 概述

智能工具柜Qt版使用MySQL数据库，支持Windows和麒麟系统。

## 数据库配置

### Windows系统（开发环境）

**数据库参数：**
- 主机：127.0.0.1
- 端口：3306
- 数据库：smart_cabinet
- 用户名：root
- 密码：root

**连接方式：**
1. **QMYSQL驱动**（推荐）
   - Qt自带驱动
   - 需要MySQL客户端库（libmysql.dll）
   - 连接速度快，稳定性好

2. **QODBC驱动**（备用）
   - 需要安装MySQL ODBC驱动
   - 连接字符串复杂
   - 适合没有QMYSQL驱动的环境

### 麒麟系统（生产环境）

**推荐连接方式：QMYSQL驱动**

麒麟系统通常已安装MySQL客户端库，QMYSQL驱动可直接使用。

**配置步骤：**

1. 安装MySQL客户端库：
   ```bash
   sudo apt-get install libmysqlclient-dev
   ```

2. 确认Qt数据库驱动：
   ```bash
   cd /path/to/Qt/plugins/sqldrivers
   ls -la | grep mysql
   ```

3. 设置环境变量（如需要）：
   ```bash
   export LD_LIBRARY_PATH=/path/to/mysql/lib:$LD_LIBRARY_PATH
   ```

## 数据库初始化

### 1. 创建数据库

```sql
CREATE DATABASE smart_cabinet CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
```

### 2. 导入表结构

使用项目根目录的`database/schema.sql`：

```bash
mysql -u root -p smart_cabinet < database/schema.sql
```

### 3. 插入初始数据

```bash
mysql -u root -p smart_cabinet < database/init_data.sql
```

## 驱动检测

### DatabaseManager自动检测逻辑

```cpp
// 优先尝试QMYSQL驱动
if (tryMySqlDriver()) {
    m_driverType = "QMYSQL";
    return true;
}

// 备用QODBC驱动
if (tryOdbcDriver()) {
    m_driverType = "QODBC";
    return true;
}
```

### 手动检测可用驱动

```cpp
qDebug() << "Available drivers:" << QSqlDatabase::drivers();
```

## 常见问题

### 1. QMYSQL驱动不可用

**错误信息：**
```
QMYSQL driver not available
```

**解决方案：**

Windows:
1. 下载MySQL Connector/C：https://dev.mysql.com/downloads/connector/c/
2. 将`libmysql.dll`复制到可执行文件目录或系统PATH
3. 确保Qt版本与MySQL客户端库位数一致（都是32位或64位）

麒麟系统:
```bash
sudo apt-get install libmysqlclient-dev
```

### 2. QODBC驱动不可用

**错误信息：**
```
QODBC driver not available
```

**解决方案：**

Windows:
1. 安装MySQL ODBC 8.0驱动：https://dev.mysql.com/downloads/connector/odbc/
2. 确保驱动名称正确：`MySQL ODBC 8.0 Unicode Driver`

麒麟系统:
```bash
sudo apt-get install unixodbc
sudo apt-get install libmyodbc
```

### 3. 数据库连接失败

**错误信息：**
```
Access denied for user 'root'@'localhost'
```

**解决方案：**
1. 确认MySQL服务已启动
2. 确认用户名密码正确
3. 确认数据库已创建
4. 检查MySQL用户权限

### 4. 麒麟系统中文乱码

**解决方案：**
1. 确保数据库使用utf8mb4字符集
2. 确保Qt应用程序使用UTF-8编码
3. 设置环境变量：`export LANG=zh_CN.UTF-8`

## 性能优化

### 1. 连接池

当前使用单连接，高并发场景建议使用连接池。

### 2. 查询优化

- 为常用查询字段添加索引
- 使用预处理查询（QSqlQuery::prepare）
- 避免在循环中执行查询

### 3. 事务处理

批量操作使用事务：
```cpp
db.transaction();
// 批量操作
db.commit();  // 或 rollback()
```

## 备份与恢复

### 备份数据库

```bash
mysqldump -u root -p smart_cabinet > backup_$(date +%Y%m%d).sql
```

### 恢复数据库

```bash
mysql -u root -p smart_cabinet < backup_20260101.sql
```

## 联系支持

如遇数据库相关问题，请联系db-agent（袁燕）。
