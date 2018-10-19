Clone repo:
```bash
eos/plugins]$ git clone https://github.com/MyWishPlatform/eosio_block_subscription_plugin/
eos/plugins]$ mv eosio_block_subscription_plugin block_subscription_plugin
```

<br />

Modify eos/plugins/CMakeLists.txt:
```
...
add_subdirectory(block_subscription_plugin)
...
```

<br />

Modify eos/programs/nodeos/CMakeLists.txt:
```
...
PRIVATE -Wl,${whole_archive_flag} block_subscription_plugin  -Wl,${no_whole_archive_flag}
...
```

<br />

Compile:
```bash
eos/build]$ make
eos/build]$ sudo make install
```

<br />

Add to config.ini:
```
...
block-subscription-interval = 500 # If you need other accepted blocks interval than default 1000
...
plugin = eosio::block_subscription_plugin
...
```
