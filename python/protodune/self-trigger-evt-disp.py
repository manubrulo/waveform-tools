import waveform_utils as wutil
import numpy as np
import matplotlib.pyplot as plt
import argparse
import re
import arrow
import os.path

def plot_with_hits(ax, s, hits=None, minmax=100, use_channel_number=True):
    wutil.plot_on_axes(ax, s, minmax=minmax, use_channel_number=use_channel_number)
    if hits is not None:
        hit_ch=hits[:,0]
        hit_t=hits[:,1]
        ax.autoscale(False)
        ax.plot(hit_t, hit_ch+0.5, "gx")

if __name__=="__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--filename", default="/data/lar/dunedaq/rodrigues/protodune-noise/np04_signal_run004643_0001_dl1_evt0.npy")
    parser.add_argument("--cmax", default=20, type=float)
    parser.add_argument("--tmin", default=None, type=float)
    parser.add_argument("--apas", default=None)
    parser.add_argument("--tmax", default=None, type=float)
    parser.add_argument("--use-channel-number", action="store_true")
    parser.add_argument("--show-hits", action="store_true")
    parser.add_argument("--batch", action="store_true")
    parser.add_argument("--save-name", default=None)
    
    parser.add_argument("--collection-only", action="store_true")
    parser.add_argument("--zoom", nargs=4, default=[], metavar=("xmin", "ymin", "width", "height"))
    parser.add_argument("--figsize", nargs=2, default=[6.4, 4.8], metavar=("width", "height"))
    args=parser.parse_args()
    a=np.load(args.filename)
    chans=a[:,1]

    if args.apas:
        apas=[int(x) for x in args.apas.split(",")]
    else:
        apas=[3,1]

    views={}
    for apa in apas:
        for view in ("u", "v", "z"):
            views.setdefault(apa, {})[view]=wutil.pedsub(wutil.get_apa(a,
                                                                       apa,
                                                                       view,
                                                                       wallorcryo="cryo" if view=="z" else "both"))
            

    # Parse the filename for run, evt, timestamp
    fname_re=re.compile(r"np04_raw_run([0-9]+)_...._dl.*_waveform_evt([0-9]+)_t(0x[0-9a-f]+)")
    m=fname_re.search(args.filename)
    if m:
        # Apparently python ignores leading zeros,
        # so don't have to worry about
        # accidentally interpreting the value as
        # octal here
        run=int(m.group(1))
        evt=int(m.group(2))
        timestamp=int(m.group(3), base=16)
        timestr=arrow.get(timestamp/50e6).format("YYYY-MM-DD HH:mm:ss UTC")
        title="Run %d, event %d (timestamp 0x%x, %s)" % (run, evt, timestamp, timestr)
    else:
        title=""

    if args.show_hits:
        hits_fname=args.filename.replace("waveform", "hits").replace(".npy", ".txt")
        hits=np.loadtxt(hits_fname)
    else:
        hits=None

    nview=1 if args.collection_only else 3
    fig,ax=plt.subplots(len(apas), nview, sharex=True, gridspec_kw=dict(top=0.85, left=0.1, right=0.95, hspace=0.02), figsize=list(map(float, args.figsize)), squeeze=False)

    for i,apa in enumerate(apas):
        plot_with_hits(ax[i,0], views[apa]["z"], hits)
        if not args.collection_only:
            plot_with_hits(ax[i,1], views[apa]["u"], hits)
            plot_with_hits(ax[i,2], views[apa]["v"], hits)

    if args.tmin is not None: ax[0,0].set_xlim(left=args.tmin)
    if args.tmax is not None: ax[0,0].set_xlim(right=args.tmax)

    ax[0,0].set_title("Z view")
    if not args.collection_only:
        ax[0,1].set_title("U view")
        ax[0,2].set_title("V view")

    fig.suptitle(title, fontsize="small")

    for a in ax.flat: a.label_outer()

    if args.save_name:
        plt.savefig(args.save_name, bbox_inches="tight", dpi=200)

    if not args.batch: plt.show()
