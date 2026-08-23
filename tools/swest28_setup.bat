cd %USERPROFILE%

winget install Kitware.CMake Ninja-build.Ninja oss-winget.gperf Python.Python.3.12 Git.Git oss-winget.dtc wget 7zip.7zip

mkdir swest28
pushd swest28
git clone https://github.com/soburi/s1c_zephyr_at_night
py -m venv .venv
call .venv\Scripts\Activate.bat
pip3 install west

west init -l s1c_zephyr_at_night

west update
cmd /c zephyr\scripts\utils\west-packages-pip-install.cmd
west zephyr-export

set PATH=%PATH%;C:\Program Files\7-Zip
echo %PATH%
west sdk install -t arm-zephyr-eabi
pyocd pack install stm32c562ret6

curl -L https://github.com/ccache/ccache/releases/download/v4.14/ccache-4.14-windows-x86_64.zip -o ccache-4.14-windows-x86_64.zip
tar -xf ccache-4.14-windows-x86_64.zip
copy ccache-4.14-windows-x86_64\ccache.exe .venv\Scripts
rmdir /S /Q ccache-4.14-windows-x86_64
