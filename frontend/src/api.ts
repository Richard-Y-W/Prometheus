const request=async(path:string,options?:RequestInit)=>{const r=await fetch(`/api${path}`,{headers:{'Content-Type':'application/json',...(options?.headers||{})},...options});if(!r.ok)throw new Error((await r.json()).detail||r.statusText);return r.json()}
export const api={
 createProject:(name:string)=>request('/projects',{method:'POST',body:JSON.stringify({name})}),
 importFixture:(id:string)=>request(`/projects/${id}/cad-imports?fixture=true`,{method:'POST'}),
 research:()=>request('/component-research',{method:'POST',body:JSON.stringify({manufacturer:'Prometheus Fixture Works',part_number:'PM-36-GM'})}),
 confirm:(id:string)=>request(`/component-packages/${id}/confirm`,{method:'POST'}),
 connect:(id:string)=>request(`/projects/${id}/connections`,{method:'POST',body:JSON.stringify({source_part:'motor',target_part:'arm',connection_type:'revolute',axis:[0,0,1],limits_deg:[0,90]})}),
 scenario:(id:string,definition:any,natural_language_description:string)=>request(`/projects/${id}/scenarios`,{method:'POST',body:JSON.stringify({name:'Motor arm duty cycle',natural_language_description,definition})}),
 compile:(id:string)=>request(`/scenarios/${id}/compile`,{method:'POST'}),
 run:(id:string)=>request(`/scenarios/${id}/runs`,{method:'POST'})}
