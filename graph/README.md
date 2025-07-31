# Environmental Data Plotter

A professional Python GUI application for downloading, analyzing, and visualizing environmental sensor data from ESP32/ESP8266 FTP servers. Built with PyQt5 for cross-platform compatibility and matplotlib for publication-quality plots.

## Features

### Data Management
- **Multi-Platform FTP Integration**: Connects to router-based FTP servers hosting ESP32/ESP8266 data
- **Automatic File Discovery**: Discovers and downloads all available CSV files from FTP server
- **Intelligent Data Parsing**: Handles both indoor (`DD_MM_YYYY.csv`) and outdoor (`DD_MM_YYYY_outside.csv`) formats
- **Data Validation**: Robust CSV parsing with error handling and data integrity checks

### Visualization Capabilities  
- **Interactive Date Range Selection**: Calendar-based date picker with validation
- **Multi-Metric Time Series**: Temperature, humidity, pressure, and sample size plots
- **Dual-Sensor Support**: Simultaneous visualization of indoor and outdoor sensor data
- **Publication-Quality Plots**: Matplotlib-generated charts with professional formatting
- **Real-Time Plot Updates**: Dynamic chart regeneration based on selected date ranges

### User Experience
- **Professional GUI**: Clean PyQt5 interface with intuitive controls
- **Progress Tracking**: Real-time progress bars and status updates during FTP operations
- **Comprehensive Logging**: Detailed console output for troubleshooting and monitoring
- **Error Recovery**: Graceful handling of network errors and data parsing issues
- **Data Export**: CSV export functionality for external analysis

## Quick Start

### Automated Setup (Windows)
1. **Launch Application**: Double-click `run_plotter.bat` in the root directory
   - Automatically creates Python virtual environment
   - Installs all required dependencies from `requirements.txt`
   - Launches the PyQt5 application

### Manual Setup (All Platforms)
```bash
# Navigate to application directory
cd graph

# Create virtual environment
python -m venv .venv

# Activate virtual environment
# Windows
.venv\Scripts\activate
# Linux/Mac  
source .venv/bin/activate

# Install dependencies
pip install -r requirements.txt

# Launch application
python environmental_plotter.py
```

### Initial Configuration
1. **Configure FTP Connection**:
   - **Server**: Router IP address (e.g., 192.168.1.1)
   - **Username**: FTP username configured on router (e.g., admin)
   - **Password**: FTP password for router access
   - **Directory**: USB storage mount path (e.g., /USB_Storage/ or /data/)

2. **Test Connection**: Click "Connect & Download Files"
   - Application connects to router FTP server
   - Downloads all available CSV files automatically
   - Displays available date range for analysis

3. **Data Analysis**:
   - **Select Date Range**: Choose start and end dates from dropdown menus
   - **Generate Plots**: Click "Generate Plot" for time series visualization
   - **Export Data**: Save filtered data to CSV for external analysis

## System Requirements

### Python Environment
- **Python Version**: 3.7 or higher
- **Operating System**: Windows 10/11, Linux (Ubuntu 18.04+), macOS 10.14+
- **Memory**: Minimum 4GB RAM (8GB recommended for large datasets)
- **Storage**: 100MB for application + space for downloaded CSV files

### Network Requirements
- **Network Access**: Connection to same network as ESP32/ESP8266 sensors
- **FTP Access**: Router FTP server must be enabled and accessible
- **Internet**: Required for initial package installation only

### Dependencies (Auto-Installed)
```python
matplotlib>=3.7.0    # Plotting and visualization engine
pandas>=2.0.0        # Data manipulation and analysis
PyQt5>=5.15.0        # Cross-platform GUI framework  
python-dateutil>=2.8.0  # Date/time parsing utilities
```

## Application Architecture

```
Environmental Plotter Application
├── Main GUI Thread (PyQt5)
│   ├── Connection Configuration Panel
│   ├── Date Range Selection Controls
│   ├── Plot Generation Interface
│   └── Progress Tracking Display
│
├── Background Worker Thread
│   ├── FTP Connection Management
│   ├── File Download Operations
│   ├── Progress Updates
│   └── Error Handling
│
├── Data Processing Engine
│   ├── CSV Parsing and Validation
│   ├── Multi-file Data Merging
│   ├── Date Range Filtering
│   └── Statistical Analysis
│
└── Visualization Engine
    ├── Matplotlib Plot Generation
    ├── Multi-subplot Layout
    ├── Time Series Formatting
    └── Export Functionality
```

## Data File Formats Supported

### ESP32 Indoor Sensor Files
- **Filename Pattern**: `DD_MM_YYYY.csv`
- **Content**: Temperature, Humidity, Pressure, Sample Size
- **Example**: `31_07_2025.csv`

### ESP8266 Outdoor Sensor Files  
- **Filename Pattern**: `DD_MM_YYYY_outside.csv`
- **Content**: Temperature, Pressure, Sample Size (no humidity)
- **Example**: `31_07_2025_outside.csv`

### CSV Structure
```csv
Date,Sample Size,Temp (°C),Pressure (hPa),Humidity (RH%)
31/07/2025 14:30,5,25.2,1013.2,65.50
31/07/2025 14:35,5,25.1,1013.1,65.30
```

## Troubleshooting Guide

### FTP Connection Issues

#### Router FTP Server Not Accessible
```bash
# Test router FTP from command line
ftp 192.168.1.1
# Expected: Connection successful, username/password prompt

# Check if USB storage is mounted
# Access router admin panel → USB Settings → Verify mount status
```

#### Authentication Failures
- Verify FTP username/password in router admin panel
- Check if FTP service is enabled on router
- Ensure FTP user has read/write permissions
- Try connecting with desktop FTP client (FileZilla) to verify credentials

#### Network Connectivity Problems
```bash
# Test basic connectivity to router
ping 192.168.1.1

# Verify you're on the same network as sensors
ipconfig /all        # Windows
ifconfig            # Linux/Mac

# Check if router FTP port is accessible
telnet 192.168.1.1 21
```

### Data Processing Issues

#### CSV Parsing Errors
- **Empty Files**: Check if sensor is successfully uploading data
- **Corrupted Data**: Verify FTP transfer mode (should be binary)
- **Date Format Issues**: Ensure sensor NTP synchronization is working
- **Character Encoding**: Files should be UTF-8 encoded

#### Missing Data Points
- **Sensor Issues**: Monitor ESP32/ESP8266 serial output for errors
- **Network Outages**: Check for gaps in data during connectivity issues
- **Storage Full**: Verify USB storage device has sufficient space

### Application Performance Issues

#### Large Dataset Handling
```python
# For datasets >100MB, consider date range filtering
# Application loads all data into memory for processing
# Recommended: Analyze data in monthly or weekly chunks
```

#### Memory Usage Optimization
- Close application between analysis sessions
- Use smaller date ranges for initial analysis
- Consider upgrading system RAM for large datasets

### Python Environment Issues

#### Virtual Environment Problems
```bash
# Recreate virtual environment if corrupted
rmdir /s .venv           # Windows
rm -rf .venv            # Linux/Mac

python -m venv .venv
.venv\Scripts\activate   # Windows
source .venv/bin/activate  # Linux/Mac
pip install -r requirements.txt
```

#### Package Installation Failures
```bash
# Update pip to latest version
python -m pip install --upgrade pip

# Install packages individually if batch install fails
pip install matplotlib
pip install pandas  
pip install PyQt5
pip install python-dateutil

# Check for system-specific Qt5 requirements
# Ubuntu/Debian: sudo apt-get install python3-pyqt5
# CentOS/RHEL: sudo yum install python3-PyQt5
```

## Advanced Features

### Custom Data Export
- Export filtered data to CSV for external analysis
- Includes metadata headers with sensor information
- Preserves original timestamp formats

### Multi-Sensor Analysis
- Simultaneous display of indoor and outdoor data
- Comparative analysis between sensor locations
- Correlation analysis between environmental parameters

### Plot Customization
- Adjustable time series formatting
- Grid overlay options
- Color scheme selection
- Export plots as PNG/PDF for reports

## Development & Extension

### Adding New Sensor Types
1. Update CSV parsing logic in `environmental_plotter.py`
2. Modify plot generation for new data columns
3. Update requirements.txt if new libraries needed

### Custom Analysis Features
1. Extend DataProcessor class for statistical analysis
2. Add new visualization types to PlotGenerator
3. Implement data filtering and aggregation functions

## Support & Community

- **Issues**: Report bugs and feature requests via project repository
- **Documentation**: Complete project documentation in main README.md
- **Examples**: Sample configurations and use cases in project wiki

### Python Issues
- Ensure Python is installed and in PATH
- Try running: `python --version` in command prompt
- If virtual environment fails, try running as administrator

### Data Issues
- Check that CSV files exist on the ESP32 FTP server
- Verify file naming format: DD_MM_YYYY.csv (e.g., 29_07_2025.csv)
- Ensure files contain properly formatted data

## Data Format Expected

The application expects CSV files with the following format:
```
Date,Sample Size,Temp (°C),Pressure (hPa),Humidity (RH%)
29/07/2025 17:11,5,25.0,997.4,51.73
29/07/2025 17:16,5,25.1,997.3,51.80
```

## Manual Installation

If the batch script doesn't work, you can manually set up the environment:

```cmd
# Create virtual environment
python -m venv .venv

# Activate virtual environment
.venv\Scripts\activate

# Install dependencies
pip install -r requirements.txt

# Run application
python environmental_plotter.py
```
