---
aliases:
  - docker compose
cssclasses:
  - tools
tags:
---
## docker的编排运行

将复杂的参数（端口、挂载卷、网络）持久化在 YAML 文件中(astrbot 示例)
```yaml
services:
  astrbot:
    image: soulter/astrbot:latest
    container_name: astrbot
    restart: always
    ports:
      - "6185:6185" # Web 控制台端口
      - "6199:6199" # 消息监听端口（根据需要开启）
    volumes:
      - ./data:/AstrBot/data
      - /etc/localtime:/etc/localtime:ro
      - /etc/timezone:/etc/timezone:ro
    environment:
      - TZ=Asia/Shanghai
```

启动命令: docker compose up -d
