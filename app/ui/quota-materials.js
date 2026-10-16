// Static reflective layers follow the quota arc without extending its geometry.
export function ringLowMaterial(radius,width,arc,prefix) {
  const circle=`cx="20" cy="20" r="${radius}" pathLength="100" transform="rotate(-90 20 20)" stroke-dasharray="${arc} 100"`;
  const surface=`<defs>
    <linearGradient id="${prefix}-metal" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff" stop-opacity=".06"/><stop offset=".2" stop-color="#fff" stop-opacity=".72"/><stop offset=".32" stop-color="#fff" stop-opacity=".08"/><stop offset=".48" stop-color="#20000e" stop-opacity=".55"/><stop offset=".59" stop-color="#fff" stop-opacity=".52"/><stop offset=".72" stop-color="#fff" stop-opacity="0"/><stop offset="1" stop-color="#18000a" stop-opacity=".5"/></linearGradient>
    <linearGradient id="${prefix}-bevel" x1="0" y1="0" x2="1" y2="1"><stop class="metal-reflection" offset="0" stop-opacity=".9"/><stop class="metal-reflection" offset=".4" stop-opacity=".4"/><stop class="metal-reflection" offset="1" stop-opacity="0"/></linearGradient>
    </defs><circle class="gauge-metal" ${circle} stroke-width="${width}" stroke="url(#${prefix}-metal)"/>
    <circle class="gauge-bevel" ${circle} stroke-width=".6" stroke="url(#${prefix}-bevel)"/>`;
  return {surface};
}
