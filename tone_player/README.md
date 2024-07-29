
# 制作音频

[音频制作地址](https://tts.waytronic.com/)

选择：客服，亲和艾夏，16K mp3

注意：现在不能直接拿到音频，需要 F12 自己查看地址，下载。重新生成需要刷新页面。


# 制作Tone音频

``` bash
cd $your_path/tone_player/tools

python2 mk_audio_tone.py -f . -r ./music

cp audio_tone_uri.h -> $your_path/tone_player/include
cp audio_tone_uri.c -> $your_path/tone_player/src
cp music/audio_tone.bin -> $your_path/tone_player

```


# 添加分区

``` bash
flash_tone,   data, 0x27,          ,    200K,

```
