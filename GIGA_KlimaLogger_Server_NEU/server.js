const express=require("express");
const fs=require("fs");
const path=require("path");
const app=express();
const PORT=process.env.PORT||10000;
const API_KEY=process.env.LOGGER_API_KEY||"";
const DATA_DIR=process.env.DATA_DIR||path.join(__dirname,"data");
const DATA_FILE=path.join(DATA_DIR,"measurements.json");
fs.mkdirSync(DATA_DIR,{recursive:true});
app.use(express.json({limit:"32kb"}));
app.use(express.static(path.join(__dirname,"public")));
let measurements=loadMeasurements();

function loadMeasurements(){
  try{
    if(!fs.existsSync(DATA_FILE)) return [];
    const x=JSON.parse(fs.readFileSync(DATA_FILE,"utf8"));
    return Array.isArray(x)?x:[];
  }catch(e){console.error("Load error:",e.message);return [];}
}
function saveMeasurements(){
  const tmp=DATA_FILE+".tmp";
  fs.writeFileSync(tmp,JSON.stringify(measurements),"utf8");
  fs.renameSync(tmp,DATA_FILE);
}
function authorized(req){return API_KEY.length>0&&req.get("X-API-Key")===API_KEY;}
function normalize(b){
  const timestamp=Number(b.timestamp),temperature=Number(b.temperature),humidity=Number(b.humidity);
  if(![timestamp,temperature,humidity].every(Number.isFinite))return null;
  if(timestamp<946684800||timestamp>4102444800)return null;
  if(temperature<-80||temperature>100||humidity<0||humidity>100)return null;
  return {timestamp:Math.floor(timestamp),temperature:Math.round(temperature*100)/100,humidity:Math.round(humidity*100)/100};
}
function newestFirst(){measurements.sort((a,b)=>b.timestamp-a.timestamp);}

app.get("/api/health",(req,res)=>res.json({ok:true,measurements:measurements.length,apiKeyConfigured:API_KEY.length>0,time:new Date().toISOString()}));

app.post("/api/measure",(req,res)=>{
  if(!authorized(req))return res.status(401).json({ok:false,error:"Unauthorized"});
  const m=normalize(req.body);
  if(!m)return res.status(400).json({ok:false,error:"Invalid measurement"});
  if(measurements.some(x=>x.timestamp===m.timestamp))
    return res.status(200).json({ok:true,duplicate:true,measurement:m,count:measurements.length});
  measurements.push(m);newestFirst();
  if(measurements.length>50000)measurements=measurements.slice(0,50000);
  try{saveMeasurements();}catch(e){console.error("Save error:",e.message);return res.status(500).json({ok:false,error:"Could not save measurement"});}
  console.log(`Measurement saved: ${new Date(m.timestamp*1000).toISOString()} | ${m.temperature.toFixed(2)} C | ${m.humidity.toFixed(2)} %`);
  res.status(201).json({ok:true,duplicate:false,measurement:m,count:measurements.length});
});

app.get("/api/latest",(req,res)=>{
  newestFirst();res.set("Cache-Control","no-store");
  res.json({ok:true,measurement:measurements.length?measurements[0]:null});
});
app.get("/api/history",(req,res)=>{
  let limit=parseInt(req.query.limit,10);if(!Number.isFinite(limit))limit=200;
  limit=Math.max(1,Math.min(limit,5000));newestFirst();res.set("Cache-Control","no-store");
  res.json({ok:true,count:measurements.length,measurements:measurements.slice(0,limit).reverse()});
});
app.get("/api/history.csv",(req,res)=>{
  newestFirst();
  const f=new Intl.DateTimeFormat("de-DE",{timeZone:"Europe/Berlin",year:"numeric",month:"2-digit",day:"2-digit",hour:"2-digit",minute:"2-digit",second:"2-digit",hourCycle:"h23"});
  const rows=["Jahr,Monat,Tag,Stunde,Minute,Sekunde,Temperatur_C,Luftfeuchtigkeit_Prozent"];
  for(const m of measurements.slice().reverse()){
    const p=f.formatToParts(new Date(m.timestamp*1000)),get=t=>p.find(x=>x.type===t)?.value||"";
    rows.push([get("year"),get("month"),get("day"),get("hour"),get("minute"),get("second"),m.temperature.toFixed(2),m.humidity.toFixed(2)].join(","));
  }
  res.setHeader("Content-Type","text/csv; charset=utf-8");
  res.setHeader("Content-Disposition",'attachment; filename="giga-klima-messungen.csv"');
  res.send("\uFEFF"+rows.join("\n"));
});
app.use("/api",(req,res)=>res.status(404).json({ok:false,error:"API route not found"}));
app.get("*",(req,res)=>res.sendFile(path.join(__dirname,"public","index.html")));
app.listen(PORT,()=>console.log(`GIGA Klima Logger listening on port ${PORT}`));
