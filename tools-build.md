### docker build

```
docker build -f dockerfile-cpp-env-1.0.7 -t ubt-cpp-env:1.0.9 .
```

### docker compose

```
# 构建所有镜像并启动
docker compose -f docker-compose.yml up -d

# 只构建不启动
docker-compose build

# 查看日志
docker-compose logs -f

# 进入开发环境
docker exec -it tinyserver-cpp-env bash

# 停止+删容器+网络（保留卷、镜像）
docker-compose down

# 重启启动
docker compose -f docker-compose.yml restart 

# 停止(保留容器+网络+卷+镜像)
docker compose -f docker-compose.yml stop
```

#### 连接mysql

```
mysql -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD $MYSQL_DATABASE\
mysql -h mysql-8-0 -P 3306 -u user -p123456 TinyDB
```

### 连接redis

```
redis-cli -h $REDIS_HOST -p $REDIS_PORT -a $REDIS_PASSWORD
redis-cli -h redis-7-2 -p 6379 -a 123456
```

<br />

###### 生成 excel 读取到proto 的工具和文件

```
# 生成pb.h, pb.cc 文件
python3 ./../../tinytools/excel2proto.py test_config.xlsx --protoc /usr/local/third/bin/protoc --outdir ./out/

#  不生成pb.h, pb.cc 文件
python3 ./../../tinytools/excel2proto.py test_config.xlsx --outdir ./out/

```

