# Environmental Data Plotter

A Python GUI application for downloading and visualizing environmental sensor data from your ESP32 FTP server.

## Features

- **FTP Integration**: Connects to your ESP32 FTP server and downloads all CSV data files
- **Date Range Selection**: Choose specific date ranges to analyze (minimum 1 day)
- **Time Series Plotting**: Visualizes temperature, humidity, pressure, and sample size over time
- **Data Export**: Export filtered data to CSV files
- **User-Friendly GUI**: Simple interface with progress tracking

## Quick Start

1. **Run the setup script**: Double-click `run_plotter.bat`
   - This will automatically create a virtual environment
   - Install all required dependencies
   - Launch the application

2. **Configure FTP Connection**:
   - Server: Your ESP32 IP address (default: 192.168.1.1)
   - Username: esp32 (or your configured username)
   - Password: esp32pass (or your configured password)
   - Directory: /data (or your data directory path)

3. **Download Data**: Click "Connect & Download Files"
   - The app will connect to your FTP server
   - Download all CSV files automatically
   - Show available date range

4. **Select Date Range**:
   - Choose start and end dates from the dropdowns
   - Must select at least 1 day of data

5. **Generate Plot**: Click "Generate Plot" to create time series charts

6. **Export Data**: Optionally export filtered data to CSV

## Requirements

- Python 3.7 or higher
- Internet connection (for downloading packages on first run)
- Access to your ESP32 FTP server

## File Structure

```
graph/
├── run_plotter.bat          # Windows batch script to run the application
├── environmental_plotter.py # Main Python application
├── requirements.txt         # Python dependencies
└── README.md               # This file
```

## Troubleshooting

### Connection Issues
- Verify ESP32 IP address is correct
- Check that FTP server is running on ESP32
- Ensure your computer is on the same network as ESP32
- Verify username/password credentials

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
