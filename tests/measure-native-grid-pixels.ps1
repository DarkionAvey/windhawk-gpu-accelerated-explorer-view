param($Directory)
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Collections.Generic;
public class GridPixels {
    static List<int[]> Runs(bool[] bits, int bridge) {
        var runs = new List<int[]>();
        for(int i=0;i<bits.Length;i++) if(bits[i]) {
            if(runs.Count==0 || i-runs[runs.Count-1][1]>bridge+1) runs.Add(new int[]{i,i});
            else runs[runs.Count-1][1]=i;
        }
        return runs;
    }
    public static void Report(string file) {
        using(var b=new Bitmap(file)) {
            var masks=new byte[b.Height,b.Width];
            for(int y=0;y<b.Height;y++) for(int x=0;x<b.Width;x++){
                var c=b.GetPixel(x,y);
                if(c.R==255 && c.G==0 && c.B==255) masks[y,x]=1;
                else if(c.R>210 && c.G>75 && c.G<245 && c.B<170 && c.R>c.G) masks[y,x]=2;
                else if(Math.Max(c.R,Math.Max(c.G,c.B))<185) masks[y,x]=3;
            }
            foreach(byte kind in new byte[]{1,2,3}){
                var ys=new bool[b.Height];
                for(int y=0;y<b.Height;y++)for(int x=0;x<b.Width;x++) if(masks[y,x]==kind)ys[y]=true;
                foreach(var row in Runs(ys,0)){
                    var xs=new bool[b.Width];
                    for(int y=row[0];y<=row[1];y++) for(int x=0;x<b.Width;x++) if(masks[y,x]==kind) xs[x]=true;
                    var spans=Runs(xs,kind==3?5:1).ConvertAll(r=>r[0]+"-"+r[1]);
                    Console.WriteLine("{0} y {1}-{2} x {3}",kind==1?"thumbnail":kind==2?"warm ink":"dark ink",row[0],row[1],string.Join(",",spans));
                }
            }
        }
    }
}
"@
foreach($preset in 'small','medium','large','extralarge'){
    Write-Host $preset
    [GridPixels]::Report((Join-Path $Directory "$preset.bmp"))
}

