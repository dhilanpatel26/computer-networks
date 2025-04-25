import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


def freq_as(csv_path):
    # what are the 10 most frequent AS numbers in the BGP paths?
    df = pd.read_csv(csv_path)
    df['ASPATH'] = df['ASPATH'].str.split(' ')
    # remove duplicates in ASPATH
    df['ASPATH'] = df['ASPATH'].apply(lambda x: list(set(x)))
    as_counts = df['ASPATH'].explode().value_counts().head(10)

    print("Top 10 AS numbers by frequency:")
    for i, (as_number, count) in enumerate(as_counts.items()):
        print(f"{i+1}. AS{as_number}: {count} occurrences")

    count_found_in = df['ASPATH'].apply(lambda x: any(as_number in x for as_number in as_counts.index)).sum()
    total_paths = len(df)
    percentage = (count_found_in / total_paths) * 100
    print(f"Percentage of top 10 AS numbers in all paths: {percentage:.2f}%")

def path_length_cdf(csv_path):
    df = pd.read_csv(csv_path)
    df['ASPATH'] = df['ASPATH'].str.split(' ')
    df['ASPATH'] = df['ASPATH'].apply(lambda x: len(set(x)))  # unique AS numbers in path\
    path_lengths = df['ASPATH'].value_counts().sort_index()
    path_lengths = path_lengths / path_lengths.sum()  # normalize to get probabilities
    cdf = path_lengths.cumsum()
    plt.figure(0)
    plt.step(path_lengths.index, cdf, where='post')
    plt.xlabel('Path Length (Number of Unique ASes)')
    plt.ylabel('CDF')
    plt.title('CDF of BGP Path Lengths')
    plt.grid(True)
    plt.tight_layout()
    plt.show()

def updates_per_minute(csv_path):
    df = pd.read_csv(csv_path)
    # how many updates per minute on average?
    mins = df['TIME'].apply(lambda x: str(x)[:2])
    updates_per_minute = df.groupby(mins).size()
    avg_updates_per_minute = updates_per_minute.mean()
    print(f"Average updates per minute: {avg_updates_per_minute:.2f}")

    # parse “MM:SS.sss” into a Timedelta (hours=0)
    df['dT'] = pd.to_timedelta('00:' + df['TIME'].astype(str))
    t0 = df['dT'].iloc[0]

    # any timestamp < t0 has wrapped past the hour, so bump it by +1h
    wrap = df['dT'] < t0
    df.loc[wrap, 'dT'] += pd.Timedelta(hours=1)

    # now build a continuous per‐second count
    df = df.set_index('dT')
    updates_per_second = df.resample('s').size()
    secs = updates_per_second.index.total_seconds() - t0.total_seconds()

    # plot using the Timedelta index
    plt.figure()
    plt.plot(secs, updates_per_second.values)
    plt.xlabel('Time (since start)')
    plt.ylabel('Updates per second')
    plt.title('BGP updates')
    plt.grid(True)
    plt.tight_layout()
    plt.show()

def top_percentage_cdf(csv_path1, csv_path2):
    df1 = pd.read_csv(csv_path1)
    df2 = pd.read_csv(csv_path2)
    df1['FROM'] = df1['FROM'].apply(lambda x: str(x).split(' ')[1])
    df2['FROM'] = df2['FROM'].apply(lambda x: str(x).split(' ')[1])

    updates_from_1 = df1['FROM'].value_counts().sort_index()
    updates_from_2 = df2['FROM'].value_counts().sort_index()
    updates_from_total = updates_from_1.add(updates_from_2, fill_value=0)
    updates_from_total = updates_from_total.sort_values(ascending=False)

    print(updates_from_total)

    top_percentages = np.arange(1, len(updates_from_total) + 1) / len(updates_from_total) * 100
    cdf = updates_from_total.cumsum() / updates_from_total.sum()

    plt.figure(2)
    plt.plot(top_percentages, cdf)
    plt.xlabel('Top Percentage of ASes')
    plt.ylabel('Cumulative Fraction of Updates')
    plt.title('CDF of Updates from Top ASes')
    plt.grid(True)
    plt.tight_layout()
    plt.show()


def main():
    # freq_as('bgp_route.csv')

    # path_length_cdf('bgp_route.csv')

    # updates_per_minute('bgp_update.csv')

    top_percentage_cdf('bgp_route.csv', 'bgp_update.csv')


if __name__ == "__main__":
    main()