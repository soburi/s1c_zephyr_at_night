cd ~

sudo apt install --no-install-recommends git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3-dev python3-venv python3-tk \
  xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1

mkdir swest28
pushd swest28
git clone https://github.com/soburi/s1c_zephyr_at_night
python3 -m venv .venv
source .venv/bin/activate
pip3 install west eclipse-zenoh

west init -l s1c_zephyr_at_night

west update
west packages pip --install
west zephyr-export

west sdk install -t arm-zephyr-eabi
pyocd pack install stm32c562ret6

if [ ! -e /etc/udev/rules.d/60-openocd.rules ] ; then
  sudo cp ~/zephyr-sdk-1.0.1/hosttools/sysroots/x86_64-pokysdk-linux/usr/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
  sudo udevadm control --reload-rules
  sudo udevadm trigger
fi
