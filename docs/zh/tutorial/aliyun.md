# 阿里云 MaxCompute

## 简介

MaxCompute 是阿里云提供的一套云原生的数仓解决方案，适合于如离线引擎这种大数据形态下的批量处理任务，关于 MaxCompute 的详细介绍，可以参考 [云原生大数据计算服务 MaxCompute](https://help.aliyun.com/product/27797.html)。如果希望整合使用 MaxCompute 的生态，OpenMLDB 可以从算力和数据源两方面进行云上形态的部署和运行。

- 算力：离线引擎任务直接提交到 MaxCompute 平台运行。
- 数据源：离线引擎可以直接读取 MaxCompute 上的数据，进行特征的离线开发和处理。
