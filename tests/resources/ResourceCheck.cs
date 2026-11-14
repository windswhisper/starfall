using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
public class ResourceCheck {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibraryEx(string p, IntPtr f, uint flags);
 [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr FindResource(IntPtr m, IntPtr name, IntPtr type);
 [DllImport("kernel32.dll")] public static extern uint SizeofResource(IntPtr m, IntPtr r);
 [DllImport("kernel32.dll")] public static extern IntPtr LoadResource(IntPtr m, IntPtr r);
 [DllImport("kernel32.dll")] public static extern IntPtr LockResource(IntPtr r);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr m);
 public static void Check(string path) {
  IntPtr m=LoadLibraryEx(path,IntPtr.Zero,2); if(m==IntPtr.Zero) throw new Exception("load failed");
  try {
  foreach(int id in new int[] {102,103,105,106,107}) {
   if(FindResource(m,(IntPtr)id,(IntPtr)10)!=IntPtr.Zero) throw new Exception("unexpected retired image "+id);
  }
  foreach(int id in new int[] {101,104,108,109,110}) { var r=FindResource(m,(IntPtr)id,(IntPtr)10); if(r==IntPtr.Zero) throw new Exception("missing "+id); var n=SizeofResource(m,r); byte[] data=new byte[n]; Marshal.Copy(LockResource(LoadResource(m,r)),data,0,(int)n); using(var s=new MemoryStream(data)) using(var b=new Bitmap(s)) { Console.WriteLine(id+": "+b.Width+"x"+b.Height+" "+b.PixelFormat); } }
  for(int id=201;id<=203;id++) { var r=FindResource(m,(IntPtr)id,(IntPtr)10); if(r==IntPtr.Zero) throw new Exception("missing audio "+id); var n=SizeofResource(m,r); if(n<12) throw new Exception("short audio "+id); byte[] data=new byte[n]; Marshal.Copy(LockResource(LoadResource(m,r)),data,0,(int)n); if(data[0]!=82||data[1]!=73||data[2]!=70||data[3]!=70||data[8]!=87||data[9]!=65||data[10]!=86||data[11]!=69) throw new Exception("invalid WAV "+id); Console.WriteLine(id+": WAV "+n+" bytes"); }
  } finally { FreeLibrary(m); }
 }
}
