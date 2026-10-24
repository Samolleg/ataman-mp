param([Parameter(Mandatory=$true)][string[]]$Maps,[Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
# Read-only MPR measurement. Match navigationHeightSample's three-vertex float
# arithmetic, including occlusion of an active water layer by higher ground.
# Compiler temporaries belong to the caller's isolated analysis folder.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;
public static class NaturalCoast220 {
  static int I(byte[] b,int p){return BitConverter.ToInt32(b,p);}
  static float Z(byte[] b,int p,float max){return BitConverter.ToUInt16(b,p)*max/65534.0f;}
  static float Nav(byte[] b,int start,int vertex,float max){
    return Z(b,start+(vertex+33)*8+2,max)/4.0f+Z(b,start+(vertex+1)*8+2,max)/4.0f+Z(b,start+vertex*8+2,max)/2.0f;
  }
  public static Dictionary<string,double> Measure(byte[] b){
    if(I(b,0)!=0x019ce23c)throw new Exception("MPR signature");
    int count=I(b,4),table=I(b,8),names=table+count*22,mp=-1;
    var sectors=new List<int>();var sx=new List<int>();var sy=new List<int>();
    for(int i=0;i<count;i++){int p=table+i*22;string name=Encoding.GetEncoding(1251).GetString(b,names+I(b,p+18),BitConverter.ToUInt16(b,p+16));
      if(name.EndsWith(".mp",StringComparison.OrdinalIgnoreCase))mp=I(b,p+8);
      var m=Regex.Match(name,@"(\d{3})(\d{3})\.sec$",RegexOptions.IgnoreCase);
      if(m.Success){sectors.Add(I(b,p+8));sx.Add(int.Parse(m.Groups[1].Value));sy.Add(int.Parse(m.Groups[2].Value));}
    }
    if(mp<0)throw new Exception("Missing MP");int w=I(b,mp+8)*16,h=I(b,mp+12)*16;float max=BitConverter.ToSingle(b,mp+4);
    var dry=new bool[w*h];var slopes=new double[w*h];for(int i=0;i<dry.Length;i++)dry[i]=true;
    for(int s=0;s<sectors.Count;s++){int start=sectors[s];bool overlay=b[start+4]==3;int tiles=start+5+1089*8*2;
      for(int y=0;y<16;y++)for(int x=0;x<16;x++){int vertex=(y*2+1)*33+x*2+1,index=(sy[s]*16+y)*w+sx[s]*16+x;
        double dx=((int)BitConverter.ToUInt16(b,start+5+(vertex+1)*8+2)-BitConverter.ToUInt16(b,start+5+(vertex-1)*8+2))*max/65535.0/2;
        double dy=((int)BitConverter.ToUInt16(b,start+5+(vertex+33)*8+2)-BitConverter.ToUInt16(b,start+5+(vertex-33)*8+2))*max/65535.0/2;
        slopes[index]=Math.Atan(Math.Sqrt(dx*dx+dy*dy))*180/Math.PI;
        if(overlay&&BitConverter.ToInt16(b,tiles+1024+(y*16+x)*2)!=-1)dry[index]=Nav(b,start+5,vertex,max)>=Nav(b,start+5+1089*8,vertex,max)+0.05;
      }
    }
    var seen=new bool[dry.Length];int cells=0,components=0,small=0,edges=0;double compact=0;
    for(int i=0;i<dry.Length;i++){int x=i%w,y=i/w;if(dry[i])cells++;if(x+1<w&&dry[i]!=dry[i+1])edges++;if(y+1<h&&dry[i]!=dry[i+w])edges++;
      if(!dry[i]||seen[i])continue;var queue=new List<int>();queue.Add(i);seen[i]=true;bool boundary=false;int perimeter=0;
      for(int k=0;k<queue.Count;k++){int a=queue[k],ax=a%w,ay=a/w;boundary|=ax==0||ay==0||ax==w-1||ay==h-1;
        foreach(int d in new int[]{-1,1,-w,w}){int j=a+d;if(j<0||j>=dry.Length||Math.Abs(j%w-ax)+Math.Abs(j/w-ay)!=1||!dry[j]){perimeter++;continue;}
          if(!seen[j]){seen[j]=true;queue.Add(j);}}
      }
      if(!boundary){components++;if(queue.Count*4<64)small++;compact+=(double)perimeter*perimeter/(4*Math.PI*queue.Count);}
    }
    var landSlopes=new List<double>();for(int i=0;i<dry.Length;i++)if(dry[i])landSlopes.Add(slopes[i]);landSlopes.Sort();
    return new Dictionary<string,double>{{"interiorComponents",components},{"islandsBelow64m2",small},{"meanIslandPerimeterCompactness",components>0?compact/components:0},{"boundaryMetres",edges*2},{"areaM2",cells*4},
      {"drySlopeP50",landSlopes.Count>0?landSlopes[(int)((landSlopes.Count-1)*0.5)]:0},{"drySlopeP90",landSlopes.Count>0?landSlopes[(int)((landSlopes.Count-1)*0.9)]:0}};
  }
}
'@
$results=@(foreach($map in $Maps){[pscustomobject]@{map=[IO.Path]::GetFileName($map);dryIslands=[NaturalCoast220]::Measure([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $map).Path))}})
[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputPath),(ConvertTo-Json -InputObject $results -Depth 5),[Text.UTF8Encoding]::new($false))
$results|ForEach-Object {Write-Output ($_.map+': components='+$_.dryIslands.interiorComponents+', below64m2='+$_.dryIslands.islandsBelow64m2)}
