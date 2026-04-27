# XHandControl Python SDK Usage

<p align="center">
  <a target="_blank" href="./README.zh-CN.md">中文</a>
</p>

## Install Dependencies
The XHandControl Python SDK is a software development kit for communication with the XHand robotic hand, allowing control of the hand's joints and reading sensor data.

## Installation 
```bash
sudo apt update && sudo apt install -y \
    cmake \
    g++ \
    libcurl4-openssl-dev \
    libssl-dev \
    nlohmann-json3-dev
```

## Run
### Hardware Serial Port Permission Settings
Before using serial communication, you need to set the serial port permissions, otherwise, you may encounter an error when trying to open it:
```bash
# The serial port ID should be modified based on the actual situation
sudo chmod 666 /dev/ttyUSB0
```

### [Recommended] Execute within a conda environment
```bash
# ❗❗Note: Due to EtherCAT communication requirements, the Python interpreter permissions in the conda environment need to be set.
# ❗❗Note: This setting grants the Python process in the current conda environment access to raw network sockets.

# Create a conda environment (optional)
conda create -n xhand python=3.10.12 -y

# Activate the conda environment
conda activate xhand

# Install xhand_controller
# Enter the xhand_control_sdk_py/ directory.
pip install -r requirements.txt
pip install xhand_controller-*-cp310-cp310-*.whl

# Set permissions for the conda interpreter
sudo setcap cap_net_raw+ep $(readlink -f $(which python3))

# Run
# Enter the xhand_control_sdk_py/ directory.
python3 xhand_control_example.py
```

### Run in the native environment (requires Python 3.10 system installation; Ubuntu 24 LTS ships with Python 3.12 by default)
```bash
# Enter the xhand_control_sdk_py/ directory.
pip install --user -r requirements.txt

# ❗❗Note: Please make sure to check your Python 3 version before installation
python3 --version

# ❗❗Note: Choose one of the following options based on your local Python environment: 
# ❗❗Note: Option 1: If your local Python environment is Python 3.10, use the following command
pip install --user xhand_controller-*-cp310-cp310-*.whl

# ❗❗Note: Option 2: If your local Python environment is Python 3.12, use the following command
pip install --user xhand_controller-*-cp312-cp312-*.whl

# Add library path
echo "$(python3 -c 'import os, sys; from importlib import util; spec = util.find_spec("xhand_controller"); print(os.path.dirname(spec.origin) if spec else "")')" | sudo tee /etc/ld.so.conf.d/xhand.conf
sudo ldconfig

# Run
# Enter the xhand_control_sdk_py/ directory.
sudo -E python3 xhand_control_example.py
```

## Fingure Joint Mode
- In the **FingerCommand_t** class, the **mode** parameter is used to control the mode of finger joints.
- The modes of finger joints and the meanings of the parameters are shown in the table below:

| Name   | Value   | Notes        |
|--------|:--------:|-------------|
| Passive Mode  | 0    |    |
| POSIPosition Control ModeTION  | 3    |    |
| Force Control Mode  | 5    |    |

## Preset Action List
* Joint Positions: Joint angle positions with values ranging from 0 to 12

|Action Name|Joint Positions|Notes|
|:---:|---|---|
|Fist|11.85, 74.58, 40, -3.08, 106.02, 110, 109.75, 107.56, 107.66, 110, 109.1, 109.15||
|Palm|0, 80.66, 33.2, 0.00, 5.11, 0, 6.53, 0, 6.76, 4.41, 10.13, 0||
|V|38.32, 90, 52.08, 6.21, 2.6, 0, 2.1, 0, 110, 110, 110, 109.23||
|OK|45.88, 41.54, 67.35, 2.22, 80.45, 70.82, 31.37, 10.39, 13.69, 16.88, 1.39, 10.55||


## Important Statement
By using the product, the user shall be deemed to have read, understood and agreed to be bound by all provisions in the User Agreement. During the use, the user shall strictly abide by the provisions in the User Agreement and use the product reasonably and lawfully. Refer to [<a target="_blank" href="./USER_AGREEMENT.md">**USER_AGREEMENT**</a>] for the User Agreement.