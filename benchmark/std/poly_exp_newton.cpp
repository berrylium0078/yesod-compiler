// https://www.luogu.com.cn/record/281230442
#include<bits/stdc++.h>
using namespace std;//判了 EOF
#define gc() (rp1==rp2&&(rp2=(rp1=buf)+fread(buf,1,IO,stdin))==rp1?EOF:*rp1++)
#define pc(a) ((wrp==obuf+IO&&(fwrite(obuf,1,IO,stdout),wrp=obuf)),(*wrp++)=a)

const int IO=1<<20,N=264000,mod=998244353,g=3;
const int expB=128,lnB=64,invB=64;
char buf[IO+1],obuf[IO+1],*wrp=obuf,*rp1,*rp2;
int nn,tmpinv[N],tmpln[N],tmpexp[N],tmpexp2[N],inv[N];
unsigned long long pw[N];

inline int read(){//[0,1e9]
    int a=0,c=gc();
    while(!isdigit(c)) c=gc();
    while(isdigit(c)) a=10*a+c-'0',c=gc();
    return a;
}

template<typename qwq>inline void write(qwq x,char c){
    int sk[20],top=0;
    do{
        sk[++top]=x%10,x/=10;
    }while(x);
    while(top) pc(sk[top--]+'0');
    pc(c);
}

inline int ksm(int a,int b){
    int ret=1;
    for(;b;b>>=1,a=1ull*a*a%mod){
        if(b&1) ret=1ull*ret*a%mod;
    }
    return ret;
}

inline void inc(int &a,int b){a+=b,(a>=mod)&&(a-=mod);}

void DIF(int *D,int lim){//正变换
	for(int mid=lim>>1;mid>=2;mid>>=1){
        for(int w=0,j=0;j<lim;j+=mid<<1,w++){
            for(int k=0,a,b;k<mid;k+=2){
                a=pw[w]*D[j+mid+k+0]%mod,D[j+mid+k+0]=D[j+k+0];
                inc(D[j+k+0],a),inc(D[j+mid+k+0],mod-a);
				b=pw[w]*D[j+mid+k+1]%mod,D[j+mid+k+1]=D[j+k+1];
                inc(D[j+k+1],b),inc(D[j+mid+k+1],mod-b);
            }
        }
	}
	for(int w=0,j=0,y;j<lim;j+=2,w++){//mid=1
		y=pw[w]*D[j+1]%mod,D[j+1]=D[j],inc(D[j],y),inc(D[j+1],mod-y);
    }
}

void DIT(int *D,int lim){//逆变换
	for(int w=0,j=0,y;j<lim;j+=2,w++){
        y=pw[w]*(mod+D[j]-D[j+1])%mod,inc(D[j],D[j+1]),D[j+1]=y;
    }
	for(int mid=2,wn;mid<lim;mid<<=1){
        for(int w=0,j=0;j<lim;j+=mid<<1,w++){
            for(int k=0,a,b;k<mid;k+=2){
                a=pw[w]*(mod+D[j+k+0]-D[j+mid+k+0])%mod;
				inc(D[j+k+0],D[j+mid+k+0]),D[j+mid+k+0]=a;
				b=pw[w]*(mod+D[j+k+1]-D[j+mid+k+1])%mod;
				inc(D[j+k+1],D[j+mid+k+1]),D[j+mid+k+1]=b;
            }
        }
    }
	reverse(D+1,D+lim);int local_inv=ksm(lim,mod-2);
    for(int i=0;i<lim;i++) D[i]=1ull*D[i]*local_inv%mod;
}
//任何数组传入前都要保证清空！！！
void poly_inv_naive(int *A,int *B,int lim){
    B[0]=ksm(A[0],mod-2);
    for(int i=1;i<lim;i++){
        for(int j=0;j<i;j++) B[i]=(B[i]+1ull*B[j]*A[i-j])%mod;
        B[i]=1ull*(mod-B[0])*B[i]%mod;
    }
}

void poly_inv(int *A,int *B,int lim){//传入不允许修改 A
    if(lim<=invB) return poly_inv_naive(A,B,lim);
    poly_inv_naive(A,B,invB);
    for(int o=invB;o<lim;o<<=1){//从 o->2o
        int limit=o<<1,m=limit<<1,u=__lg(limit);
        for(int i=0;i<limit;i++) tmpinv[i]=A[i];
        DIF(tmpinv,m),DIF(B,m);
        for(int i=0;i<m;i++) B[i]=B[i]*(mod+2-1ull*B[i]*tmpinv[i]%mod)%mod;
        DIT(B,m);
        for(int i=limit;i<m;i++) B[i]=0;//注意截断   
    }
	for(int i=0;i<(lim<<1);i++) tmpinv[i]=0;//注意清空
}

void diff(int *A,int lim){//求导，原地变换
	for(int i=1;i<lim;i++) A[i-1]=1ull*i*A[i]%mod;
	A[lim-1]=0;
}

void itg(int *A,int lim){//积分，原地变换
	for(int i=lim-2;i>=0;i--) A[i+1]=1ull*inv[i+1]*A[i]%mod;
	A[0]=0; 
}

void poly_ln(int *A,int *B,int lim){
	int m=lim<<1,u=__lg(lim);
	for(int i=1;i<lim;i++) B[i-1]=1ull*i*A[i]%mod;
	for(int i=lim-1;i<m;i++) B[i]=0;//原地求导
	poly_inv(A,tmpln,lim),DIF(tmpln,m),DIF(B,m);
	for(int i=0;i<m;i++) B[i]=1ull*tmpln[i]*B[i]%mod;
	DIT(B,m);
	for(int i=lim;i<m;i++) B[i]=0;
	for(int i=0;i<m;i++) tmpln[i]=0;
	itg(B,lim);
}

void poly_exp_naive(int *A,int *B,int lim){
	B[0]=1;
	for(int i=0;i<lim;i++) tmpexp[i]=1ull*A[i]*i%mod;
	for(int i=1;i<lim;i++){
		for(int j=1;j<=i;j++) B[i]=(B[i]+1ull*tmpexp[j]*B[i-j])%mod;
		B[i]=1ull*B[i]*inv[i]%mod;
	}
	for(int i=0;i<lim;i++) tmpexp[i]=0;
}

void poly_exp(int *A,int *B,int lim){//不对 A 进行修改
	if(lim<=expB) return poly_exp_naive(A,B,lim);
	poly_exp_naive(A,B,expB);
	for(int o=expB;o<lim;o<<=1){
		int limit=o<<1,m=limit<<1;
		for(int i=0;i<limit;i++) tmpexp2[i]=A[i];
		poly_ln(B,tmpexp,limit),DIF(tmpexp,m),DIF(tmpexp2,m),DIF(B,m);
		for(int i=0;i<m;i++) B[i]=1ull*B[i]*(mod+1+tmpexp2[i]-tmpexp[i])%mod;
		DIT(B,m);
		for(int i=limit;i<m;i++) B[i]=0;
	}
	for(int i=0;i<(lim<<1);i++) tmpexp[i]=0,tmpexp2[i]=0;
}

int A[N],B[N];

int main(){
    int lim=1,m;
    for(nn=read(),inv[1]=1,pw[0]=1;lim<nn;lim<<=1){
        for(int w=ksm(g,(mod>>2)/lim),i=0;i<lim;i++) pw[lim+i]=pw[i]*w%mod;
    }m=lim<<1;
    for(int i=0;i<nn;i++) A[i]=read();  
    for(int i=2;i<m;i++) inv[i]=mod-1ull*(mod/i)*inv[mod%i]%mod;
    poly_exp(A,B,lim);
    for(int i=nn;i<m;i++) B[i]=0;//清空多余位
    for(int i=0;i<nn;i++) write(B[i],' ');
    return fwrite(obuf,1,wrp-obuf,stdout),0;
}