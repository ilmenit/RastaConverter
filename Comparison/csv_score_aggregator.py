import pandas as pd
import os
import glob
from pathlib import Path

def aggregate_csv_scores(input_directory, output_filename="aggregated_scores.csv"):
    """
    Reads all CSV files from a directory and creates a new CSV with Score columns
    from each file, using Iterations as the index.
    
    Args:
        input_directory (str): Path to directory containing CSV files
        output_filename (str): Name of output CSV file
    """
    
    print(f"🔍 Looking for CSV files in directory: {input_directory}")
    
    # Check if directory exists
    if not os.path.exists(input_directory):
        print(f"❌ Error: Directory '{input_directory}' does not exist!")
        return False
    
    # Find all CSV files in the directory
    csv_files = glob.glob(os.path.join(input_directory, "*.csv"))
    
    if not csv_files:
        print(f"❌ Error: No CSV files found in '{input_directory}'!")
        return False
    
    print(f"📁 Found {len(csv_files)} CSV files:")
    for file in csv_files:
        print(f"   - {os.path.basename(file)}")
    
    # Dictionary to store DataFrames
    all_data = {}
    successful_files = 0
    
    # Process each CSV file
    for csv_file in csv_files:
        try:
            # Get filename without extension for column name
            filename = Path(csv_file).stem
            print(f"\n📖 Processing: {filename}.csv")
            
            # Read CSV file
            df = pd.read_csv(csv_file)
            
            # Validate required columns
            required_columns = ['Iterations', 'Seconds', 'Score']
            missing_columns = [col for col in required_columns if col not in df.columns]
            
            if missing_columns:
                print(f"⚠️  Warning: {filename}.csv missing columns: {missing_columns}")
                print(f"   Available columns: {list(df.columns)}")
                continue
            
            # Check for empty data
            if df.empty:
                print(f"⚠️  Warning: {filename}.csv is empty!")
                continue
            
            # Calculate total time difference (last - first seconds)
            if len(df) >= 2:
                first_seconds = df['Seconds'].iloc[0]
                last_seconds = df['Seconds'].iloc[-1]
                time_diff = last_seconds - first_seconds
                column_name = f"{filename} ({time_diff})"
                print(f"   - Time difference: {time_diff} seconds (from {first_seconds} to {last_seconds})")
            elif len(df) == 1:
                column_name = f"{filename} (0)"
                print(f"   - Single row: no time difference")
            else:
                column_name = filename
                print(f"   - No time calculation possible")
            
            # Set Iterations as index and extract Score column
            df_indexed = df.set_index('Iterations')['Score']
            df_indexed.name = column_name  # Name the series with filename and time diff
            
            # Store in dictionary
            all_data[column_name] = df_indexed
            successful_files += 1
            
            print(f"✅ Successfully processed {filename}.csv:")
            print(f"   - Column name: '{column_name}'")
            print(f"   - Iterations range: {df['Iterations'].min()} to {df['Iterations'].max()}")
            print(f"   - Number of records: {len(df)}")
            print(f"   - Score range: {df['Score'].min():.6f} to {df['Score'].max():.6f}")
            
        except Exception as e:
            print(f"❌ Error processing {csv_file}: {str(e)}")
            continue
    
    # Check if we have any successful data
    if not all_data:
        print("\n❌ Error: No valid CSV files were processed!")
        return False
    
    print(f"\n🔄 Successfully processed {successful_files} files. Creating aggregated CSV...")
    
    try:
        # Combine all data into a single DataFrame
        # This will automatically align data based on Iterations index
        combined_df = pd.DataFrame(all_data)
        
        # Sort columns alphabetically
        sorted_columns = sorted(combined_df.columns)
        combined_df = combined_df[sorted_columns]
        
        # Reset index to make Iterations a column again
        combined_df.reset_index(inplace=True)
        
        # Sort by Iterations for cleaner output
        combined_df.sort_values('Iterations', inplace=True)
        
        # Save to CSV
        output_path = os.path.join(input_directory, output_filename)
        combined_df.to_csv(output_path, index=False)
        
        print(f"✅ Aggregated CSV created successfully!")
        print(f"📄 Output file: {output_path}")
        print(f"📊 Final dataset info:")
        print(f"   - Total unique iterations: {len(combined_df)}")
        print(f"   - Iteration range: {combined_df['Iterations'].min()} to {combined_df['Iterations'].max()}")
        print(f"   - Number of score columns: {len(combined_df.columns) - 1}")
        print(f"   - Column names: {list(combined_df.columns)}")
        
        # Check for missing data
        total_cells = len(combined_df) * (len(combined_df.columns) - 1)  # -1 for Iterations column
        missing_cells = combined_df.iloc[:, 1:].isna().sum().sum()  # Skip Iterations column
        if missing_cells > 0:
            print(f"📋 Missing data info:")
            print(f"   - Total missing values: {missing_cells} out of {total_cells} cells")
            print(f"   - Missing data per column:")
            for col in combined_df.columns[1:]:  # Skip Iterations
                missing_count = combined_df[col].isna().sum()
                if missing_count > 0:
                    print(f"     • {col}: {missing_count} missing values")
        else:
            print(f"✅ No missing data - all files have identical iteration ranges!")
        
        # Display first few rows as preview
        print(f"\n📋 Preview (first 5 rows):")
        print(combined_df.head())
        
        return True
        
    except Exception as e:
        print(f"❌ Error creating aggregated CSV: {str(e)}")
        return False

def main():
    """
    Main function to run the CSV aggregation.
    """
    print("🚀 CSV Score Aggregator Started")
    print("=" * 50)
    
    # You can modify this path to your directory
    input_dir = input("Enter the directory path containing CSV files (or press Enter for current directory): ").strip()
    
    if not input_dir:
        input_dir = "."  # Current directory
    
    # Optional: custom output filename
    output_name = input("Enter output filename (or press Enter for 'aggregated_scores.csv'): ").strip()
    if not output_name:
        output_name = "aggregated_scores.csv"
    
    # Run the aggregation
    success = aggregate_csv_scores(input_dir, output_name)
    
    if success:
        print("\n🎉 Process completed successfully!")
    else:
        print("\n💥 Process failed. Please check the error messages above.")

if __name__ == "__main__":
    main()
