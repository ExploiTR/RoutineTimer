#!/usr/bin/env python3
"""
Environmental Data Plotter - PyQt5 Version
Downloads data from FTP server and creates time series plots
"""

import sys
import logging
import traceback
from datetime import datetime as dt
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QGridLayout, QLabel, QLineEdit, 
                             QPushButton, QComboBox, QProgressBar, QMessageBox,
                             QFileDialog, QGroupBox, QStatusBar)
from PyQt5.QtCore import Qt, QThread, pyqtSignal
from PyQt5.QtGui import QFont
import matplotlib.pyplot as plt
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure
from matplotlib.dates import DateFormatter
import pandas as pd
from datetime import datetime, timedelta
import ftplib
import io
import os
import re
from typing import List, Dict, Tuple, Optional

# Configure logging
def setup_logging():
    """Setup comprehensive logging for the application"""
    
    # Create formatter
    formatter = logging.Formatter(
        '%(asctime)s | %(levelname)-8s | %(name)-20s | %(funcName)-20s | Line:%(lineno)-4d | %(message)s'
    )
    
    # Console handler only
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(logging.DEBUG)
    console_handler.setFormatter(formatter)
    
    # Root logger
    root_logger = logging.getLogger()
    root_logger.setLevel(logging.DEBUG)
    root_logger.addHandler(console_handler)
    
    # Create application logger
    logger = logging.getLogger('EnvironmentalPlotter')
    logger.info("Logging initialized - Console output only")
    logger.info(f"Application started at {dt.now()}")
    
    return logger

# Initialize logging
logger = setup_logging()


class FTPDataManager:
    """Handles FTP connection and data download"""
    
    def __init__(self):
        self.logger = logging.getLogger('FTPDataManager')
        self.logger.debug("FTPDataManager initialized")
        self.host = ""
        self.username = ""
        self.password = ""
        self.directory = ""
        self.connection = None
    
    def connect(self, host: str, username: str, password: str, directory: str = "") -> bool:
        """Connect to FTP server"""
        self.logger.info(f"Attempting FTP connection to {host}:21")
        self.logger.debug(f"Connection parameters - Host: {host}, Username: {username}, Directory: '{directory}'")
        
        try:
            self.host = host
            self.username = username
            self.password = password
            self.directory = directory
            
            self.logger.debug("Creating FTP connection object")
            self.connection = ftplib.FTP()
            
            self.logger.debug(f"Connecting to {host}:21 with 30s timeout")
            self.connection.connect(host, 21, timeout=30)
            self.logger.info("TCP connection to FTP server established")
            
            self.logger.debug(f"Logging in with username: {username}")
            self.connection.login(username, password)
            self.logger.info("FTP login successful")
            
            if directory:
                self.logger.debug(f"Changing to directory: {directory}")
                self.connection.cwd(directory)
                self.logger.info(f"Successfully changed to directory: {directory}")
            else:
                self.logger.debug("No directory specified, staying in root")
            
            self.logger.info("FTP connection fully established")
            return True
            
        except ftplib.error_perm as e:
            self.logger.error(f"FTP Permission error: {e}")
            return False
        except ftplib.error_temp as e:
            self.logger.error(f"FTP Temporary error: {e}")
            return False
        except ConnectionRefusedError as e:
            self.logger.error(f"Connection refused: {e}")
            return False
        except TimeoutError as e:
            self.logger.error(f"Connection timeout: {e}")
            return False
        except OSError as e:
            self.logger.error(f"Network error: {e}")
            return False
        except Exception as e:
            self.logger.error(f"Unexpected FTP connection error: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            return False
    
    def disconnect(self):
        """Disconnect from FTP server"""
        self.logger.debug("Attempting to disconnect from FTP server")
        
        if self.connection:
            try:
                self.logger.debug("Sending QUIT command to FTP server")
                self.connection.quit()
                self.logger.info("FTP connection closed gracefully")
            except (ftplib.error_temp, ftplib.error_perm, OSError) as e:
                self.logger.warning(f"Error during graceful disconnect: {e}, forcing close")
                try:
                    self.connection.close()
                    self.logger.info("FTP connection forcefully closed")
                except Exception as e:
                    self.logger.error(f"Error forcing connection close: {e}")
            
            self.connection = None
            self.logger.debug("FTP connection object cleared")
        else:
            self.logger.debug("No active FTP connection to disconnect")
    
    def list_csv_files(self) -> List[str]:
        """List all CSV files on the FTP server"""
        self.logger.info("Starting to list CSV files on FTP server")
        
        if not self.connection:
            self.logger.error("No active FTP connection available")
            return []
        
        try:
            self.logger.debug("Executing LIST command on FTP server")
            file_list = []
            self.connection.retrlines('LIST', file_list.append)
            self.logger.debug(f"Received {len(file_list)} file entries from server")
            
            csv_files = []
            for i, line in enumerate(file_list):
                self.logger.debug(f"Processing file entry {i+1}: {line}")
                
                # Parse FTP LIST output (format may vary by server)
                parts = line.split()
                if len(parts) >= 9 and parts[-1].endswith('.csv'):
                    filename = parts[-1]
                    self.logger.debug(f"Found CSV file: {filename}")
                    
                    # Look for date pattern DD_MM_YYYY.csv or DD_MM_YYYY_outside.csv
                    if re.match(r'\d{2}_\d{2}_\d{4}\.csv', filename) or re.match(r'\d{2}_\d{2}_\d{4}_outside\.csv', filename):
                        csv_files.append(filename)
                        self.logger.debug(f"CSV file matches date pattern: {filename}")
                    else:
                        self.logger.debug(f"CSV file does not match date pattern: {filename}")
                else:
                    self.logger.debug(f"File entry ignored (not CSV or invalid format): {line}")
            
            sorted_files = sorted(csv_files)
            self.logger.info(f"Found {len(sorted_files)} valid CSV files with date pattern")
            self.logger.debug(f"CSV files found: {sorted_files}")
            
            return sorted_files
            
        except ftplib.error_perm as e:
            self.logger.error(f"Permission error listing files: {e}")
            return []
        except ftplib.error_temp as e:
            self.logger.error(f"Temporary error listing files: {e}")
            return []
        except Exception as e:
            self.logger.error(f"Unexpected error listing files: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            return []
    
    def download_file(self, filename: str) -> Optional[str]:
        """Download a file and return its content as string"""
        self.logger.info(f"Starting download of file: {filename}")
        
        if not self.connection:
            self.logger.error("No active FTP connection available for download")
            return None
        
        try:
            self.logger.debug(f"Creating binary buffer for file: {filename}")
            file_content = io.BytesIO()
            
            self.logger.debug(f"Executing RETR command for: {filename}")
            self.connection.retrbinary(f'RETR {filename}', file_content.write)
            
            file_size = file_content.tell()
            self.logger.debug(f"Downloaded {file_size} bytes from {filename}")
            
            self.logger.debug(f"Decoding content from {filename} as UTF-8")
            content = file_content.getvalue().decode('utf-8')
            
            lines_count = len(content.split('\n'))
            self.logger.info(f"Successfully downloaded {filename}: {file_size} bytes, {lines_count} lines")
            self.logger.debug(f"First 100 characters of {filename}: {content[:100]}")
            
            return content
            
        except ftplib.error_perm as e:
            self.logger.error(f"Permission error downloading {filename}: {e}")
            return None
        except ftplib.error_temp as e:
            self.logger.error(f"Temporary error downloading {filename}: {e}")
            return None
        except UnicodeDecodeError as e:
            self.logger.error(f"UTF-8 decode error for {filename}: {e}")
            return None
        except Exception as e:
            self.logger.error(f"Unexpected error downloading {filename}: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            return None


class FTPDownloadThread(QThread):
    """Thread for downloading FTP data without blocking UI"""
    
    progress_updated = pyqtSignal(int)
    status_updated = pyqtSignal(str)
    download_complete = pyqtSignal(dict, dict, list)
    download_error = pyqtSignal(str)
    
    def __init__(self, host, username, password, directory):
        super().__init__()
        self.logger = logging.getLogger('FTPDownloadThread')
        self.logger.debug("FTPDownloadThread initialized")
        
        self.host = host
        self.username = username
        self.password = password
        self.directory = directory
        
        self.logger.debug(f"Thread configured - Host: {host}, Username: {username}, Directory: '{directory}'")
    
    def run(self):
        """Run the download process in a separate thread"""
        self.logger.info("Starting FTP download thread")
        ftp_manager = FTPDataManager()
        
        try:
            self.logger.debug("Emitting connection status update")
            self.status_updated.emit("Connecting to FTP server...")
            
            self.logger.info("Initiating FTP connection from thread")
            # Connect to FTP
            success = ftp_manager.connect(self.host, self.username, self.password, self.directory)
            
            if not success:
                error_msg = "Failed to connect to FTP server"
                self.logger.error(error_msg)
                self.download_error.emit(error_msg)
                return
            
            self.logger.info("FTP connection successful, proceeding to file listing")
            self.status_updated.emit("Listing CSV files...")
            
            # Get list of CSV files
            csv_files = ftp_manager.list_csv_files()
            self.logger.debug(f"Retrieved file list: {csv_files}")
            
            if not csv_files:
                error_msg = "No CSV files found on the server"
                self.logger.warning(error_msg)
                self.download_error.emit(error_msg)
                return
            
            self.logger.info(f"Found {len(csv_files)} CSV files to download")
            self.status_updated.emit(f"Found {len(csv_files)} files. Downloading...")
            
            # Download all files
            data_cache = {}
            outdoor_data_cache = {}
            available_dates = []
            
            for i, filename in enumerate(csv_files):
                progress = int((i / len(csv_files)) * 100)
                self.logger.debug(f"Download progress: {progress}% ({i+1}/{len(csv_files)})")
                self.progress_updated.emit(progress)
                self.status_updated.emit(f"Downloading {filename}...")
                
                self.logger.info(f"Downloading file {i+1}/{len(csv_files)}: {filename}")
                content = ftp_manager.download_file(filename)
                
                if content:
                    self.logger.debug(f"Successfully downloaded {filename}, processing date")
                    
                    # Check if it's an outdoor file
                    is_outdoor = filename.endswith('_outside.csv')
                    
                    # Parse date from filename
                    if is_outdoor:
                        date_match = re.match(r'(\d{2})_(\d{2})_(\d{4})_outside\.csv', filename)
                    else:
                        date_match = re.match(r'(\d{2})_(\d{2})_(\d{4})\.csv', filename)
                    
                    if date_match:
                        day, month, year = date_match.groups()
                        date_str = f"{day}/{month}/{year}"
                        
                        if is_outdoor:
                            outdoor_data_cache[date_str] = content
                            self.logger.debug(f"Outdoor file {filename} mapped to date: {date_str}")
                        else:
                            data_cache[date_str] = content
                            self.logger.debug(f"Indoor file {filename} mapped to date: {date_str}")
                        
                        # Add to available dates if not already present
                        if date_str not in available_dates:
                            available_dates.append(date_str)
                    else:
                        self.logger.warning(f"File {filename} does not match expected date pattern")
                else:
                    self.logger.error(f"Failed to download content for {filename}")
            
            self.logger.info("Disconnecting from FTP server")
            # Disconnect from FTP
            ftp_manager.disconnect()
            
            self.logger.debug(f"Sorting {len(available_dates)} dates")
            # Sort dates
            available_dates.sort(key=lambda x: datetime.strptime(x, "%d/%m/%Y"))
            self.logger.debug(f"Sorted dates: {available_dates}")
            
            self.logger.info(f"Download process completed successfully: {len(available_dates)} files")
            self.progress_updated.emit(100)
            self.status_updated.emit(f"Successfully downloaded {len(available_dates)} files")
            self.download_complete.emit(data_cache, outdoor_data_cache, available_dates)
            
        except Exception as e:
            error_msg = f"Error during download: {str(e)}"
            self.logger.error(error_msg)
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            self.download_error.emit(error_msg)
        finally:
            self.logger.debug("Ensuring FTP connection is closed in finally block")
            ftp_manager.disconnect()
            self.logger.info("FTP download thread completed")


class MatplotlibCanvas(FigureCanvas):
    """Custom matplotlib canvas for PyQt5"""
    
    def __init__(self, parent=None):
        self.logger = logging.getLogger('MatplotlibCanvas')
        self.logger.debug("Initializing matplotlib canvas")
        
        self.figure = Figure(figsize=(12, 8))
        super().__init__(self.figure)
        self.setParent(parent)
        
        self.logger.debug("Creating 2x2 subplot layout")
        # Create subplots
        self.axes = self.figure.subplots(2, 2)
        self.figure.tight_layout(pad=3.0)
        
        # Initialize hover annotation variables
        self.current_df = None
        self.hover_annotation = None
        
        # Connect mouse motion event
        self.mpl_connect('motion_notify_event', self.on_hover)
        
        # Initial empty plot
        self.clear_plots()
        self.logger.info("Matplotlib canvas initialized successfully")
    
    def clear_plots(self):
        """Clear all plots"""
        self.logger.debug("Clearing all plots")
        
        # Clear stored data for hover functionality
        self.current_df = None
        
        try:
            for i, ax in enumerate(self.axes.flat):
                ax.clear()
                self.logger.debug(f"Cleared subplot {i}")
            
            self.axes[0, 0].set_title("No Data Available")
            self.axes[0, 0].text(0.5, 0.5, "Connect to FTP and select date range\nto view environmental data", 
                                ha='center', va='center', transform=self.axes[0, 0].transAxes, fontsize=12)
            
            for i, ax in enumerate(self.axes.flat):
                if i > 0:
                    ax.set_visible(False)
                    self.logger.debug(f"Hid subplot {i}")
            
            self.draw()
            self.logger.info("Plots cleared and canvas updated")
            
        except Exception as e:
            self.logger.error(f"Error clearing plots: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
    
    def on_hover(self, event):
        """Handle mouse hover events to show data point values"""
        if event.inaxes is None or self.current_df is None or len(self.current_df) == 0:
            # Clear any existing annotations
            if hasattr(self, 'hover_annotation') and self.hover_annotation:
                self.hover_annotation.set_visible(False)
                self.draw_idle()
            return
        
        # Clear previous annotation
        if hasattr(self, 'hover_annotation') and self.hover_annotation:
            self.hover_annotation.set_visible(False)
        
        # Check which subplot we're in and find the closest data point
        ax = event.inaxes
        x_pos = event.xdata
        y_pos = event.ydata
        
        if x_pos is None or y_pos is None:
            self.draw_idle()
            return
        
        # Find the closest data point
        try:
            # Convert matplotlib date number to datetime for comparison
            from matplotlib.dates import num2date
            hover_time = num2date(x_pos)
            
            # Make sure both datetime objects are timezone-naive for comparison
            if hover_time.tzinfo is not None:
                hover_time = hover_time.replace(tzinfo=None)
            
            # Find closest time point in data
            time_diffs = abs(self.current_df['datetime'] - hover_time)
            closest_idx = time_diffs.idxmin()
            closest_point = self.current_df.iloc[closest_idx]
            
            # Check if we're close enough to the point (within reasonable distance)
            time_tolerance = pd.Timedelta(hours=2)  # 2 hours tolerance
            if time_diffs.iloc[closest_idx] > time_tolerance:
                return
            
            # Determine which plot we're hovering over and get appropriate values
            annotation_text = ""
            display_x = closest_point['datetime']
            display_y = 0
            
            if ax == self.axes[0, 0]:  # Temperature plot
                display_y = closest_point['temperature']
                annotation_text = f"Time: {display_x.strftime('%d/%m/%Y %H:%M')}\nIndoor Temp: {display_y:.1f}°C"
            elif ax == self.axes[0, 1]:  # Humidity plot
                display_y = closest_point['humidity']
                annotation_text = f"Time: {display_x.strftime('%d/%m/%Y %H:%M')}\nHumidity: {display_y:.1f}%RH"
            elif ax == self.axes[1, 0]:  # Pressure plot
                display_y = closest_point['pressure']
                annotation_text = f"Time: {display_x.strftime('%d/%m/%Y %H:%M')}\nIndoor Pressure: {display_y:.1f}hPa"
            elif ax == self.axes[1, 1]:  # Feels like temperature plot
                if 'feels_like' in closest_point:
                    display_y = closest_point['feels_like']
                    actual_temp = closest_point['temperature']
                    annotation_text = f"Time: {display_x.strftime('%d/%m/%Y %H:%M')}\nFeels Like: {display_y:.1f}°C\nActual: {actual_temp:.1f}°C"
                else:
                    display_y = closest_point['temperature']
                    annotation_text = f"Time: {display_x.strftime('%d/%m/%Y %H:%M')}\nTemp: {display_y:.1f}°C"
            
            if annotation_text:
                # Create annotation at the data point
                self.hover_annotation = ax.annotate(
                    annotation_text,
                    xy=(display_x, display_y),
                    xytext=(20, 20),
                    textcoords='offset points',
                    bbox={'boxstyle': 'round,pad=0.5', 'fc': 'lightyellow', 'alpha': 0.9, 'edgecolor': 'gray'},
                    arrowprops={'arrowstyle': '->', 'connectionstyle': 'arc3,rad=0', 'color': 'gray'},
                    fontsize=9,
                    zorder=1000
                )
                
            self.draw_idle()
            
        except Exception as e:
            self.logger.debug(f"Error in hover handler: {e}")
    
    def calculate_heat_index(self, temp_c: float, humidity: float) -> float:
        """Calculate heat index (feels like temperature) from temperature and humidity"""
        try:
            # Convert Celsius to Fahrenheit
            temp_f = (temp_c * 9/5) + 32
            
            # Heat index is only meaningful for temps > 80°F (26.7°C)
            if temp_f < 80.0:
                return temp_c  # Return actual temperature in Celsius
            
            T = temp_f
            R = humidity
            
            # Basic Heat Index calculation (Rothfusz equation)
            HI = (-42.379 + 2.04901523*T + 10.14333127*R - 0.22475541*T*R
                  - 6.83783e-3*T*T - 5.481717e-2*R*R + 1.22874e-3*T*T*R
                  + 8.5282e-4*T*R*R - 1.99e-6*T*T*R*R)
            
            # Adjustments for extreme conditions
            if R < 13 and T >= 80 and T <= 112:
                HI = HI - ((13-R)/4) * (((17-abs(T-95))/17)**0.5)
            elif R > 85 and T >= 80 and T <= 87:
                HI = HI + ((R-85)/10) * ((87-T)/5)
            
            # Convert back to Celsius
            return (HI - 32) * 5/9
            
        except Exception as e:
            self.logger.warning(f"Error calculating heat index: {e}")
            return temp_c  # Return original temperature on error
    
    def create_time_series_plots(self, indoor_df: pd.DataFrame, outdoor_df: pd.DataFrame = None):
        """Create time series plots"""
        self.logger.info(f"Creating time series plots for {len(indoor_df)} indoor data points")
        if outdoor_df is not None and not outdoor_df.empty:
            self.logger.info(f"Also plotting {len(outdoor_df)} outdoor data points")
        
        # Store DataFrame for hover functionality (using indoor data as primary)
        self.current_df = indoor_df.copy()
        
        try:
            # Clear previous plots
            self.logger.debug("Clearing previous plots")
            for ax in self.axes.flat:
                ax.clear()
                ax.set_visible(True)
            
            # Plot 1: Temperature (Indoor and Outdoor)
            self.logger.debug("Creating temperature plot")
            self.axes[0, 0].plot(indoor_df['datetime'], indoor_df['temperature'], 'r-', linewidth=1.5, label='Indoor Temperature')
            if outdoor_df is not None and not outdoor_df.empty:
                self.axes[0, 0].plot(outdoor_df['datetime'], outdoor_df['temperature'], 'orange', linewidth=1.5, label='Outdoor Temperature')
            
            self.axes[0, 0].set_title('Temperature Over Time')
            self.axes[0, 0].set_ylabel('Temperature (°C)')
            self.axes[0, 0].grid(True, alpha=0.3)
            self.axes[0, 0].tick_params(axis='x', rotation=45)
            self.axes[0, 0].legend()
            
            temp_range_indoor = f"Indoor: {indoor_df['temperature'].min():.1f}°C to {indoor_df['temperature'].max():.1f}°C"
            self.logger.debug(f"Temperature range - {temp_range_indoor}")
            if outdoor_df is not None and not outdoor_df.empty:
                temp_range_outdoor = f"Outdoor: {outdoor_df['temperature'].min():.1f}°C to {outdoor_df['temperature'].max():.1f}°C"
                self.logger.debug(f"Temperature range - {temp_range_outdoor}")
            
            # Plot 2: Humidity (Indoor only - outdoor sensor doesn't have humidity)
            self.logger.debug("Creating humidity plot")
            self.axes[0, 1].plot(indoor_df['datetime'], indoor_df['humidity'], 'b-', linewidth=1.5, label='Indoor Humidity')
            self.axes[0, 1].set_title('Humidity Over Time')
            self.axes[0, 1].set_ylabel('Humidity (%RH)')
            self.axes[0, 1].grid(True, alpha=0.3)
            self.axes[0, 1].tick_params(axis='x', rotation=45)
            self.axes[0, 1].legend()
            humidity_range = f"{indoor_df['humidity'].min():.1f}% to {indoor_df['humidity'].max():.1f}%"
            self.logger.debug(f"Humidity range: {humidity_range}")
            
            # Plot 3: Pressure (Indoor and Outdoor)
            self.logger.debug("Creating pressure plot")
            self.axes[1, 0].plot(indoor_df['datetime'], indoor_df['pressure'], 'g-', linewidth=1.5, label='Indoor Pressure')
            if outdoor_df is not None and not outdoor_df.empty:
                self.axes[1, 0].plot(outdoor_df['datetime'], outdoor_df['pressure'], 'purple', linewidth=1.5, label='Outdoor Pressure')
            
            self.axes[1, 0].set_title('Atmospheric Pressure Over Time')
            self.axes[1, 0].set_ylabel('Pressure (hPa)')
            self.axes[1, 0].grid(True, alpha=0.3)
            self.axes[1, 0].tick_params(axis='x', rotation=45)
            self.axes[1, 0].legend()
            
            pressure_range_indoor = f"Indoor: {indoor_df['pressure'].min():.1f}hPa to {indoor_df['pressure'].max():.1f}hPa"
            self.logger.debug(f"Pressure range - {pressure_range_indoor}")
            if outdoor_df is not None and not outdoor_df.empty:
                pressure_range_outdoor = f"Outdoor: {outdoor_df['pressure'].min():.1f}hPa to {outdoor_df['pressure'].max():.1f}hPa"
                self.logger.debug(f"Pressure range - {pressure_range_outdoor}")
            
            # Plot 4: Feels Like Temperature (Heat Index)
            self.logger.debug("Creating feels like temperature plot")
            # Calculate feels like temperature for indoor data
            feels_like_temp = []
            for _, row in indoor_df.iterrows():
                feels_like = self.calculate_heat_index(row['temperature'], row['humidity'])
                feels_like_temp.append(feels_like)
            
            indoor_df_copy = indoor_df.copy()
            indoor_df_copy['feels_like'] = feels_like_temp
            
            self.axes[1, 1].plot(indoor_df_copy['datetime'], indoor_df_copy['feels_like'], 'darkred', linewidth=1.5, marker='o', markersize=1, label='Feels Like')
            self.axes[1, 1].plot(indoor_df_copy['datetime'], indoor_df_copy['temperature'], 'lightcoral', linewidth=1, alpha=0.7, label='Actual Temp')
            self.axes[1, 1].set_title('Feels Like Temperature Over Time')
            self.axes[1, 1].set_ylabel('Temperature (°C)')
            self.axes[1, 1].set_xlabel('Date/Time')
            self.axes[1, 1].grid(True, alpha=0.3)
            self.axes[1, 1].tick_params(axis='x', rotation=45)
            self.axes[1, 1].legend()
            
            feels_like_range = f"{min(feels_like_temp):.1f}°C to {max(feels_like_temp):.1f}°C"
            self.logger.debug(f"Feels like temperature range: {feels_like_range}")
            
            # Update stored data for hover functionality to include feels like temp
            self.current_df['feels_like'] = feels_like_temp
            
            # Format x-axis
            self.logger.debug("Formatting x-axis for all plots")
            for ax in self.axes.flat:
                ax.tick_params(axis='x', labelsize=8)
            
            self.figure.tight_layout()
            self.draw()
            
            self.logger.info("Time series plots created and displayed successfully")
            
        except Exception as e:
            self.logger.error(f"Error creating time series plots: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            raise


class EnvironmentalDataPlotter(QMainWindow):
    """Main application class"""
    
    def __init__(self):
        super().__init__()
        self.logger = logging.getLogger('EnvironmentalDataPlotter')
        self.logger.info("Starting Environmental Data Plotter application")
        
        self.logger.debug("Initializing main window properties")
        self.setWindowTitle("Environmental Data Plotter")
        self.setGeometry(100, 100, 1200, 800)
        
        self.data_cache = {}  # Cache downloaded indoor data
        self.outdoor_data_cache = {}  # Cache downloaded outdoor data
        self.available_dates = []
        
        self.logger.debug("Setting up user interface")
        self.setup_ui()
        
        self.logger.info("Application initialization complete")
    
    def setup_ui(self):
        """Create the user interface"""
        self.logger.debug("Creating central widget and main layout")
        
        try:
            # Central widget
            central_widget = QWidget()
            self.setCentralWidget(central_widget)
            
            # Main layout
            main_layout = QVBoxLayout(central_widget)
            
            # FTP Connection Group
            self.logger.debug("Setting up FTP connection group")
            self.setup_ftp_group(main_layout)
            
            # Date Selection Group
            self.logger.debug("Setting up date selection group")
            self.setup_date_group(main_layout)
            
            # Plot Area
            self.logger.debug("Setting up plot area")
            self.setup_plot_area(main_layout)
            
            # Status Bar
            self.logger.debug("Setting up status bar")
            self.status_bar = QStatusBar()
            self.setStatusBar(self.status_bar)
            self.status_bar.showMessage("Ready")
            
            self.logger.info("UI setup completed successfully")
            
        except Exception as e:
            self.logger.error(f"Error setting up UI: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            raise
    
    def setup_ftp_group(self, parent_layout):
        """Setup FTP connection controls"""
        self.logger.debug("Creating FTP connection group")
        
        try:
            ftp_group = QGroupBox("FTP Connection")
            ftp_layout = QGridLayout(ftp_group)
            
            self.logger.debug("Adding FTP form fields")
            
            # Server
            ftp_layout.addWidget(QLabel("Server:"), 0, 0)
            self.server_edit = QLineEdit("192.168.0.1")
            ftp_layout.addWidget(self.server_edit, 0, 1)
            
            # Username
            ftp_layout.addWidget(QLabel("Username:"), 0, 2)
            self.username_edit = QLineEdit("admin")
            ftp_layout.addWidget(self.username_edit, 0, 3)
            
            # Password
            ftp_layout.addWidget(QLabel("Password:"), 0, 4)
            self.password_edit = QLineEdit("f6a3067773")
            self.password_edit.setEchoMode(QLineEdit.Password)
            ftp_layout.addWidget(self.password_edit, 0, 5)
            
            # Directory
            ftp_layout.addWidget(QLabel("Directory:"), 1, 0)
            self.directory_edit = QLineEdit("/G/USD_TPL/")
            ftp_layout.addWidget(self.directory_edit, 1, 1)
            
            # Connect button
            self.logger.debug("Adding connect button and progress bar")
            self.connect_btn = QPushButton("Connect & Download Files")
            self.connect_btn.clicked.connect(self.connect_and_download)
            ftp_layout.addWidget(self.connect_btn, 1, 2, 1, 2)
            
            # Progress bar
            self.progress_bar = QProgressBar()
            ftp_layout.addWidget(self.progress_bar, 1, 4, 1, 2)
            
            parent_layout.addWidget(ftp_group)
            self.logger.debug("FTP connection group created successfully")
            
        except Exception as e:
            self.logger.error(f"Error setting up FTP group: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            raise
    
    def setup_date_group(self, parent_layout):
        """Setup date selection controls"""
        self.logger.debug("Creating date selection group")
        
        try:
            date_group = QGroupBox("Date Range Selection")
            date_layout = QGridLayout(date_group)
            
            self.logger.debug("Adding date selection components")
            
            # Available dates info
            date_layout.addWidget(QLabel("Available Dates:"), 0, 0)
            self.dates_info_label = QLabel("No data loaded")
            self.dates_info_label.setStyleSheet("color: blue;")
            date_layout.addWidget(self.dates_info_label, 0, 1)
            
            # Start date
            date_layout.addWidget(QLabel("Start Date:"), 1, 0)
            self.start_date_combo = QComboBox()
            date_layout.addWidget(self.start_date_combo, 1, 1)
            
            # End date
            date_layout.addWidget(QLabel("End Date:"), 1, 2)
            self.end_date_combo = QComboBox()
            date_layout.addWidget(self.end_date_combo, 1, 3)
            
            # Plot button
            self.plot_btn = QPushButton("Generate Plot")
            self.plot_btn.clicked.connect(self.generate_plot)
            self.plot_btn.setEnabled(False)
            date_layout.addWidget(self.plot_btn, 1, 4)
            
            # Export button
            self.export_btn = QPushButton("Export Data")
            self.export_btn.clicked.connect(self.export_data)
            self.export_btn.setEnabled(False)
            date_layout.addWidget(self.export_btn, 1, 5)
            
            parent_layout.addWidget(date_group)
            self.logger.debug("Date selection group created successfully")
            
        except Exception as e:
            self.logger.error(f"Error setting up date group: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            raise
    
    def setup_plot_area(self, parent_layout):
        """Setup matplotlib plot area"""
        self.logger.debug("Creating plot area")
        
        try:
            plot_group = QGroupBox("Environmental Data Plot")
            plot_layout = QVBoxLayout(plot_group)
            
            # Create matplotlib canvas
            self.logger.debug("Initializing matplotlib canvas")
            self.canvas = MatplotlibCanvas()
            plot_layout.addWidget(self.canvas)
            
            parent_layout.addWidget(plot_group)
            self.logger.debug("Plot area created successfully")
            
        except Exception as e:
            self.logger.error(f"Error setting up plot area: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            raise
    
    def connect_and_download(self):
        """Connect to FTP and download all CSV files"""
        self.logger.info("Starting FTP connection and download process")
        
        try:
            server = self.server_edit.text()
            username = self.username_edit.text()
            directory = self.directory_edit.text()
            self.logger.debug(f"FTP connection details - Server: {server}, Username: {username}, Directory: {directory}")
            
            self.connect_btn.setEnabled(False)
            self.progress_bar.setValue(0)
            
            # Start download thread
            self.logger.debug("Creating and starting FTP download thread")
            self.download_thread = FTPDownloadThread(
                server,
                username,
                self.password_edit.text(),
                directory
            )
            
            # Connect signals
            self.logger.debug("Connecting thread signals")
            self.download_thread.progress_updated.connect(self.progress_bar.setValue)
            self.download_thread.status_updated.connect(self.status_bar.showMessage)
            self.download_thread.download_complete.connect(self.on_download_complete)
            self.download_thread.download_error.connect(self.on_download_error)
            
            # Start download
            self.download_thread.start()
            self.logger.info("FTP download thread started successfully")
            
        except Exception as e:
            self.logger.error(f"Error starting FTP download: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            self.connect_btn.setEnabled(True)
            QMessageBox.critical(self, "Error", f"Failed to start download: {str(e)}")
    
    def on_download_complete(self, data_cache, outdoor_data_cache, available_dates):
        """Handle successful download completion"""
        self.logger.info(f"Download completed successfully - {len(available_dates)} files downloaded")
        self.logger.debug(f"Available dates: {available_dates}")
        
        try:
            self.data_cache = data_cache
            self.outdoor_data_cache = outdoor_data_cache
            self.available_dates = available_dates
            
            self.logger.debug("Updating date selection dropdowns")
            self.update_date_selection()
            self.connect_btn.setEnabled(True)
            
            indoor_count = len(data_cache)
            outdoor_count = len(outdoor_data_cache)
            total_files = indoor_count + outdoor_count
            
            self.logger.info("Download process completed and UI updated")
            QMessageBox.information(self, "Success", f"Downloaded {total_files} data files ({indoor_count} indoor, {outdoor_count} outdoor)")
            
        except Exception as e:
            self.logger.error(f"Error handling download completion: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            QMessageBox.critical(self, "Error", f"Error processing downloaded data: {str(e)}")
    
    def on_download_error(self, error_message):
        """Handle download error"""
        self.logger.error(f"Download failed: {error_message}")
        
        try:
            self.connect_btn.setEnabled(True)
            self.status_bar.showMessage("Download failed")
            QMessageBox.critical(self, "Download Error", error_message)
            
        except Exception as e:
            self.logger.error(f"Error handling download error: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
    
    def update_date_selection(self):
        """Update date selection dropdowns"""
        self.logger.debug("Updating date selection dropdowns")
        
        if not self.available_dates:
            self.logger.warning("No available dates to update selection")
            return
        
        try:
            # Update info label
            start_date = self.available_dates[0]
            end_date = self.available_dates[-1]
            self.logger.debug(f"Date range: {start_date} to {end_date} ({len(self.available_dates)} days)")
            self.dates_info_label.setText(f"{start_date} to {end_date} ({len(self.available_dates)} days)")
            
            # Update dropdowns
            self.logger.debug("Populating date dropdowns")
            self.start_date_combo.clear()
            self.end_date_combo.clear()
            self.start_date_combo.addItems(self.available_dates)
            self.end_date_combo.addItems(self.available_dates)
            
            # Set default selection
            self.start_date_combo.setCurrentText(start_date)
            self.end_date_combo.setCurrentText(end_date)
            
            # Enable buttons
            self.plot_btn.setEnabled(True)
            self.export_btn.setEnabled(True)
            
            self.logger.info("Date selection updated successfully")
            
        except Exception as e:
            self.logger.error(f"Error updating date selection: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
    
    def parse_csv_content(self, content: str) -> pd.DataFrame:
        """Parse CSV content into pandas DataFrame"""
        self.logger.debug(f"Parsing CSV content ({len(content)} characters)")
        
        try:
            lines = content.strip().split('\n')
            self.logger.debug(f"CSV has {len(lines)} lines")
            
            # Skip header if present
            data_lines = []
            for line in lines:
                if line.strip() and not line.startswith('Date,Sample'):
                    data_lines.append(line.strip())
            
            self.logger.debug(f"Found {len(data_lines)} data lines after filtering")
            
            if not data_lines:
                self.logger.warning("No data lines found in CSV content")
                return pd.DataFrame()
            
            # Parse data
            parsed_data = []
            for i, line in enumerate(data_lines):
                try:
                    parts = line.split(',')
                    if len(parts) >= 5:
                        # Format: "DD/MM/YYYY HH:MM,sample_size,temperature,pressure,humidity"
                        datetime_str = parts[0].strip()  # Already contains both date and time
                        sample_size = int(parts[1].strip())
                        temperature = float(parts[2].strip())
                        pressure = float(parts[3].strip())
                        humidity = float(parts[4].strip())
                        
                        parsed_data.append({
                            'datetime': datetime_str,
                            'sample_size': sample_size,
                            'temperature': temperature,
                            'pressure': pressure,
                            'humidity': humidity
                        })
                        self.logger.debug(f"Parsed line {i+1}: T={temperature}°C, P={pressure}hPa, H={humidity}%RH")
                    else:
                        self.logger.warning(f"Line {i+1} has insufficient data ({len(parts)} parts): {line}")
                        
                except (ValueError, IndexError) as e:
                    self.logger.warning(f"Error parsing line {i+1}: {line} - {e}")
                    continue
            
            if not parsed_data:
                self.logger.warning("No valid data parsed from CSV")
                return pd.DataFrame()
            
            df = pd.DataFrame(parsed_data)
            df['datetime'] = pd.to_datetime(df['datetime'], format='%d/%m/%Y %H:%M')
            
            self.logger.info(f"Successfully parsed {len(df)} records from CSV")
            self.logger.debug(f"Data range: {df['datetime'].min()} to {df['datetime'].max()}")
            
            return df
            
        except Exception as e:
            self.logger.error(f"Error parsing CSV content: {e}")
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            return pd.DataFrame()
    
    def generate_plot(self):
        """Generate time series plots for selected date range"""
        self.logger.info("Starting plot generation")
        
        start_date = self.start_date_combo.currentText()
        end_date = self.end_date_combo.currentText()
        
        self.logger.debug(f"Selected date range: {start_date} to {end_date}")
        
        if not start_date or not end_date:
            self.logger.warning("No start or end date selected")
            QMessageBox.warning(self, "Selection Error", "Please select both start and end dates")
            return
        
        try:
            # Validate date range
            start_dt = datetime.strptime(start_date, "%d/%m/%Y")
            end_dt = datetime.strptime(end_date, "%d/%m/%Y")
            
            if start_dt > end_dt:
                self.logger.warning(f"Invalid date range: start ({start_date}) > end ({end_date})")
                QMessageBox.warning(self, "Date Error", "Start date must be before or equal to end date")
                return
            
            self.status_bar.showMessage("Processing data and generating plots...")
            self.logger.debug("Collecting data for selected date range")
            
            # Collect indoor data for selected range
            indoor_data = []
            outdoor_data = []
            dates_to_process = []
            
            # Find all dates in range
            current_dt = start_dt
            while current_dt <= end_dt:
                date_str = current_dt.strftime("%d/%m/%Y")
                if date_str in self.data_cache:
                    dates_to_process.append(date_str)
                current_dt += timedelta(days=1)
            
            self.logger.debug(f"Found {len(dates_to_process)} dates with indoor data in selected range")
            
            # Parse indoor data for each date
            for date_str in dates_to_process:
                self.logger.debug(f"Processing indoor data for {date_str}")
                content = self.data_cache[date_str]
                df = self.parse_csv_content(content)
                if not df.empty:
                    indoor_data.append(df)
                    self.logger.debug(f"Added {len(df)} indoor records from {date_str}")
                else:
                    self.logger.warning(f"No valid indoor data found for {date_str}")
            
            # Parse outdoor data for each date (if available)
            for date_str in dates_to_process:
                if date_str in self.outdoor_data_cache:
                    self.logger.debug(f"Processing outdoor data for {date_str}")
                    content = self.outdoor_data_cache[date_str]
                    df = self.parse_csv_content(content)
                    if not df.empty:
                        outdoor_data.append(df)
                        self.logger.debug(f"Added {len(df)} outdoor records from {date_str}")
                    else:
                        self.logger.warning(f"No valid outdoor data found for {date_str}")
                else:
                    self.logger.debug(f"No outdoor data available for {date_str}")
            
            if not indoor_data:
                self.logger.warning("No indoor data found in selected date range")
                QMessageBox.warning(self, "No Data", "No indoor data available for the selected date range")
                self.status_bar.showMessage("Ready")
                return
            
            # Combine indoor data
            combined_indoor_df = pd.concat(indoor_data, ignore_index=True)
            combined_indoor_df = combined_indoor_df.sort_values('datetime')
            
            # Combine outdoor data if available
            combined_outdoor_df = None
            if outdoor_data:
                combined_outdoor_df = pd.concat(outdoor_data, ignore_index=True)
                combined_outdoor_df = combined_outdoor_df.sort_values('datetime')
                self.logger.info(f"Combined outdoor data: {len(combined_outdoor_df)} total records")
            
            self.logger.info(f"Combined indoor data: {len(combined_indoor_df)} total records")
            self.logger.debug(f"Indoor data time range: {combined_indoor_df['datetime'].min()} to {combined_indoor_df['datetime'].max()}")
            
            # Create plots
            self.logger.debug("Creating time series plots")
            self.canvas.create_time_series_plots(combined_indoor_df, combined_outdoor_df)
            
            plot_info = f"Plot generated successfully - {len(combined_indoor_df)} indoor data points"
            if combined_outdoor_df is not None:
                plot_info += f", {len(combined_outdoor_df)} outdoor data points"
            
            self.status_bar.showMessage(plot_info)
            self.logger.info("Plot generation completed successfully")
            
        except Exception as e:
            error_msg = f"Error generating plot: {str(e)}"
            self.logger.error(error_msg)
            self.logger.debug(f"Full traceback: {traceback.format_exc()}")
            QMessageBox.critical(self, "Plot Error", error_msg)
            self.status_bar.showMessage("Plot generation failed")
    
    def export_data(self):
        """Export selected data to CSV file"""
        self.logger.info("Starting data export")
        
        start_date = self.start_date_combo.currentText()
        end_date = self.end_date_combo.currentText()
        
        self.logger.debug(f"Export date range: {start_date} to {end_date}")
        
        if not start_date or not end_date:
            self.logger.warning("No start or end date selected for export")
            QMessageBox.warning(self, "Selection Error", "Please select both start and end dates")
            return
        
        try:
            # Get save location
            self.logger.debug("Opening file save dialog")
            filename, _ = QFileDialog.getSaveFileName(
                self, "Export Environmental Data", "", "CSV files (*.csv);;All files (*.*)"
            )
            
            if not filename:
                self.logger.info("Export cancelled by user")
                return
            
            self.logger.info(f"Exporting data to: {filename}")
            
            # Validate date range
            start_dt = datetime.strptime(start_date, "%d/%m/%Y")
            end_dt = datetime.strptime(end_date, "%d/%m/%Y")
            
            # Collect and combine indoor data
            indoor_data = []
            outdoor_data = []
            current_date = start_dt
            
            while current_date <= end_dt:
                date_str = current_date.strftime("%d/%m/%Y")
                if date_str in self.data_cache:
                    df = self.parse_csv_content(self.data_cache[date_str])
                    if not df.empty:
                        indoor_data.append(df)
                
                # Check for outdoor data
                if date_str in self.outdoor_data_cache:
                    df_outdoor = self.parse_csv_content(self.outdoor_data_cache[date_str])
                    if not df_outdoor.empty:
                        outdoor_data.append(df_outdoor)
                
                current_date += timedelta(days=1)
            
            if not indoor_data:
                QMessageBox.warning(self, "No Data", "No indoor data available for selected date range")
                return
            
            # Combine indoor data
            combined_indoor_df = pd.concat(indoor_data, ignore_index=True)
            combined_indoor_df = combined_indoor_df.sort_values('datetime')
            
            # Add feels like temperature to indoor data
            feels_like_temp = []
            canvas = MatplotlibCanvas()  # Create temporary instance for calculation
            for _, row in combined_indoor_df.iterrows():
                feels_like = canvas.calculate_heat_index(row['temperature'], row['humidity'])
                feels_like_temp.append(feels_like)
            combined_indoor_df['feels_like'] = feels_like_temp
            
            # Format datetime for export
            combined_indoor_df['Date/Time'] = combined_indoor_df['datetime'].dt.strftime('%d/%m/%Y %H:%M')
            
            # Prepare export dataframe
            export_columns = ['Date/Time', 'sample_size', 'temperature', 'pressure', 'humidity', 'feels_like']
            export_df = combined_indoor_df[export_columns].copy()
            export_df.columns = ['Date/Time', 'Sample Size', 'Indoor Temperature (°C)', 'Indoor Pressure (hPa)', 'Humidity (%RH)', 'Feels Like (°C)']
            
            # Add outdoor data if available
            if outdoor_data:
                combined_outdoor_df = pd.concat(outdoor_data, ignore_index=True)
                combined_outdoor_df = combined_outdoor_df.sort_values('datetime')
                combined_outdoor_df['Date/Time'] = combined_outdoor_df['datetime'].dt.strftime('%d/%m/%Y %H:%M')
                
                # Merge outdoor temperature and pressure data
                outdoor_export = combined_outdoor_df[['Date/Time', 'temperature', 'pressure']]
                outdoor_export.columns = ['Date/Time', 'Outdoor Temperature (°C)', 'Outdoor Pressure (hPa)']
                
                # Merge with indoor data on Date/Time
                export_df = pd.merge(export_df, outdoor_export, on='Date/Time', how='left')
            
            export_df.to_csv(filename, index=False)
            
            export_info = f"Data exported successfully to {filename}"
            if outdoor_data:
                export_info += f" (includes {len(indoor_data)} indoor and {len(outdoor_data)} outdoor data files)"
            else:
                export_info += f" (includes {len(indoor_data)} indoor data files)"
            
            QMessageBox.information(self, "Export Success", export_info)
            self.status_bar.showMessage(f"Data exported to {os.path.basename(filename)}")
            
        except Exception as e:
            QMessageBox.critical(self, "Export Error", f"Error exporting data: {str(e)}")


def main():
    """Main application entry point"""
    logger = logging.getLogger('main')
    logger.info("Starting Environmental Data Plotter application")
    
    try:
        app = QApplication(sys.argv)
        app.setApplicationName("Environmental Data Plotter")
        app.setOrganizationName("ESP32 Environmental Monitor")
        
        logger.debug("Setting application style")
        # Set application style
        app.setStyle('Fusion')
        
        # Create and show main window
        logger.debug("Creating main window")
        window = EnvironmentalDataPlotter()
        window.show()
        
        logger.info("Application started successfully, entering event loop")
        # Run application
        sys.exit(app.exec_())
        
    except Exception as e:
        logger.error(f"Fatal error starting application: {e}")
        logger.debug(f"Full traceback: {traceback.format_exc()}")
        if 'app' in locals():
            QMessageBox.critical(None, "Fatal Error", f"Failed to start application: {str(e)}")
        sys.exit(1)


if __name__ == "__main__":
    main()
