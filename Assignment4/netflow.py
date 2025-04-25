import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


def plot_bytes_cdf(csv_path):
    df = pd.read_csv(csv_path)
    sorted_bytes = np.sort(df['Bytes'].values)
    cdf = np.arange(1, len(sorted_bytes) + 1) / len(sorted_bytes)
    plt.figure(0)
    plt.step(sorted_bytes, cdf, where='post')
    plt.xlabel('Bytes in Flow')
    plt.xscale('log')
    plt.yscale('linear')
    plt.ylabel('CDF')
    plt.title('CDF of Flow Bytes (All Flows)')
    plt.grid(True)
    plt.tight_layout()

    filtered_df = df[df['Protocol'] == 'TCP']
    sorted_bytes = np.sort(filtered_df['Bytes'].values)
    cdf = np.arange(1, len(sorted_bytes) + 1) / len(sorted_bytes)
    plt.figure(1)
    plt.step(sorted_bytes, cdf, where='post')
    plt.xlabel('Bytes in Flow')
    plt.xscale('log')
    plt.yscale('linear')
    plt.ylabel('CDF')
    plt.title('CDF of Flow Bytes (TCP Flows)')
    plt.grid(True)
    plt.tight_layout()

    filtered_df = df[df['Protocol'] == 'UDP']
    sorted_bytes = np.sort(filtered_df['Bytes'].values)
    cdf = np.arange(1, len(sorted_bytes) + 1) / len(sorted_bytes)
    plt.figure(2)
    plt.step(sorted_bytes, cdf, where='post')
    plt.xlabel('Bytes in Flow')
    plt.xscale('log')
    plt.yscale('linear')
    plt.ylabel('CDF')
    plt.title('CDF of Flow Bytes (UDP Flows)')
    plt.grid(True)
    plt.tight_layout()

    plt.show()

def top_ips(csv_path):
    df = pd.read_csv(csv_path)
    # top src ips by flows
    df['Src IP addr'] = df['Src IP addr'].str.split('.').str[:2].str.join('.')

    top_src_ips = df['Src IP addr'].value_counts().head(10)

    print("Top 10 Source IPs by flows:")
    for i, (ip, count) in enumerate(top_src_ips.items()):
        print(f"{i+1}. {ip}: {count} flows")

    total_flows = len(df)
    top_src_flows = top_src_ips.sum()
    percentage_top_src = (top_src_flows / total_flows) * 100
    print(f"Percentage of flows from top 10 source IPs: {percentage_top_src:.2f}%\n")

    # top src ips by bytes
    top_src_bytes = df.groupby('Src IP addr')['Bytes'].sum().nlargest(10)
    top_src_ips = top_src_bytes.index
    print("Top 10 Source IPs by bytes:")
    for i, ip in enumerate(top_src_ips):
        print(f"{i+1}. {ip}: {top_src_bytes[ip]} bytes")

    total_bytes = df['Bytes'].sum()
    top_src_bytes_sum = top_src_bytes.sum()
    percentage_top_src_bytes = (top_src_bytes_sum / total_bytes) * 100
    print(f"Percentage of bytes from top 10 source IPs: {percentage_top_src_bytes:.2f}%\n")

def port_analysis(csv_path, port):
    df = pd.read_csv(csv_path)

    port_flows = df[df['Src port'] == port]
    total_flows = len(df)
    port_flows_count = len(port_flows)
    percentage = (port_flows_count / total_flows) * 100
    print(f"Percentage of flows from port {port}: {percentage:.2f}%")

    port_flows = df[df['Dst port'] == port]
    total_flows = len(df)
    port_flows_count = len(port_flows)
    percentage = (port_flows_count / total_flows) * 100
    print(f"Percentage of flows to port {port}: {percentage:.2f}%")

def router_bytes(csv_path):
    THIS_ROUTER = '128.112'
    df = pd.read_csv(csv_path)
    df['Src IP addr'] = df['Src IP addr'].str.split('.').str[:2].str.join('.')
    df['Dst IP addr'] = df['Dst IP addr'].str.split('.').str[:2].str.join('.')

    router_send_bytes = df[df['Src IP addr'] == THIS_ROUTER]['Bytes'].sum()
    router_recv_bytes = df[df['Dst IP addr'] == THIS_ROUTER]['Bytes'].sum()
    total_bytes = df['Bytes'].sum()

    print(f"Percentage of total bytes sent by {THIS_ROUTER}: {router_send_bytes / total_bytes * 100:.2f}%")
    print(f"Percentage of total bytes received by {THIS_ROUTER}: {router_recv_bytes / total_bytes * 100:.2f}%")

    router_bytes = df[(df['Src IP addr'] == THIS_ROUTER) & (df['Dst IP addr'] == THIS_ROUTER)]['Bytes'].sum()
    print(f"Percentage of bytes both sent and received by {THIS_ROUTER}: {router_bytes / total_bytes * 100:.2f}%")
    

def main():
    csv_path = 'netflow.csv'
    # plot_bytes_cdf(csv_path)

    # top_ips(csv_path)

    # port_analysis(csv_path, port=443)

    # router_bytes(csv_path)

if __name__ == "__main__":
    main()