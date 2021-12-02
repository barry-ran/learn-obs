# learn-obs
学习obs

# 搭建libobs开发环境
## mac
- [release](https://github.com/obsproject/obs-studio/releases/tag/27.1.3)页面下载27.1.3版本的dmg安装包和对应源码
- 打开dmg安装obs
- 解压源码obs-studio放在learn-obs同级目录
- Qt Creator打开示例项目的CMakeLists.txt即可编译运行

## 跟踪调试
上面使用的是release版本的obs，调试问题跟踪源码不方便，可以自己编译debug版本方便调试
- 在[这个页面](https://github.com/obsproject/obs-studio/wiki/Install-Instructions#windows-build-directions)下载预编译的依赖包
- 在obs-studio根目录执行下面脚本生成obs xcode项目（方便编译release）（如果生成有问题，可以在plugins/CMakeLists.txt中手动注释掉不需要编译的插件）
```
mkdir build && cd build
cmake -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13  -DSWIGDIR="/Users/bytedance/Downloads/macos-deps-2021-11-30-x86_64" -DDepsPath="/Users/bytedance/Downloads/macos-deps-2021-11-30-x86_64" -DDISABLE_PYTHON=ON -DENABLE_UI=OFF -DBUILD_BROWSER=OFF -DBUILD_VST=OFF -G Xcode  ..
```
- 使用xcode打开项目编译
- 生成的debug版obs在build/rundir目录
- 修改example项目的CMakeLists.txt依赖自己编译的debug版本obs即可跟踪调试了

# 参考文档
- [obs源码仓库](https://github.com/obsproject/obs-studio)
- [libobs官方文档](https://obsproject.com/docs/)
- [obs模块分析](https://www.jianshu.com/p/d47bba75582b)
- [obs主要线程](https://blog.csdn.net/qq_33588386/article/details/112556804)
- [obs优秀博客](https://blog.csdn.net/qq_33588386/category_10663820.html)
