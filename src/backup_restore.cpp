#include "backup_restore.h"
#include "message_store.h"
#include "map_tiles.h"
#include "hardware/storage.h"
#include "protocol/mesh_protocol.h"
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <string.h>

namespace {
constexpr uint32_t MAGIC=0x3142494DU; // MIB1
constexpr uint16_t FORMAT_VERSION=1;
constexpr size_t MAX_ENTRIES=60;
constexpr size_t MAX_PART_BYTES=256000;
constexpr char LEAF_NVS[]="@meshtastic";
constexpr char CORE_NVS[]="@mesh-auth";
constexpr char SHARED_NAME[]="@node-name";
constexpr char CORE_MESSAGES[]="/meshcore_messages.bin";
constexpr char LEAF_MESSAGES[]="/meshtastic_messages.bin";
constexpr char LEAF_NODES[]="/meshtastic_nodes.bin";
struct __attribute__((packed)) Header {
    uint32_t magic;
    uint16_t version;
    uint8_t protocol;
    uint8_t categories;
    uint16_t count;
    uint16_t reserved;
};
struct __attribute__((packed)) Part {
    uint8_t category;
    char path[47];
    uint32_t size;
    uint32_t crc;
};
struct __attribute__((packed)) LeafSettings {
    uint32_t magic;
    uint8_t region,preset,hop,has_private,has_public;
    uint8_t private_key[32];
    uint8_t public_key[32];
};
static_assert(sizeof(Header)==12,"Backup file header must be stable");
static_assert(sizeof(Part)==56,"Backup entry header must be stable");
static_assert(sizeof(LeafSettings)==73,"Leaf settings wire structure changed");
struct Plan {
    Part part{};
};
struct Stage {
    char path[48]{};
    char staged[20]{};
    char previous[20]{};
    uint8_t category=0;
    bool old_exists=false;
    bool moved_old=false;
    bool committed=false;
};
static char last_error[84]="";
static uint8_t io[512];
static uint8_t nvs_data[4096];
static uint8_t previous_nvs[4096];
static size_t previous_nvs_size=0;
static LeafSettings leaf_data{},old_leaf_data{};
static Plan plans[MAX_ENTRIES];
static Stage stages[MAX_ENTRIES];
constexpr char RESTORE_TXN[]="/restore.txn";
constexpr char RESTORE_TXN_TEMP[]="/restore.txn.tmp";
constexpr uint32_t RESTORE_MAGIC=0x31585452U; // RTX1
struct RestoreHeader {uint32_t magic;uint16_t version;uint16_t count;};
static_assert(sizeof(RestoreHeader)==8,"Stable restore journal header required");

static bool fail(const char* reason){
    snprintf(last_error,sizeof(last_error),"%s",reason);
    Serial.printf("[T5-BACKUP] %s\n",last_error);
    return false;
}
static uint32_t crc32(const uint8_t* data,size_t count,uint32_t crc=0xFFFFFFFFU){
    for(size_t i=0;i<count;++i){
        crc^=data[i];
        for(int bit=0;bit<8;++bit)
            crc=(crc>>1)^((crc&1)?0xEDB88320U:0);
    }
    return crc;
}
static bool sd_ready(){
    if(!map_tiles_media_ready())return false;
    // The map service can retain an inconclusive mount when the filesystem
    // contains no map archive. Require a readable root for backup/restore.
    File root=meshink_storage_open("/");
    const bool ok=root&&root.isDirectory();
    if(root)root.close();
    return ok;
}
static bool sd_writable(){
    if(!sd_ready())return fail("Insert a readable SD card");
    // Never overwrite an existing map, screenshot, or previous backup.
    char path[20]{};
    bool found=false;
    for(unsigned i=0;i<100;++i){
        snprintf(path,sizeof(path),"/MIBCHK%02u.TMP",i);
        if(!meshink_storage_exists(path)){found=true;break;}
    }
    if(!found)return fail("No safe SD test filename");
    const uint8_t sample[8]={'M','E','S','H','I','N','K','!'};
    File writer=meshink_storage_open_write(path);
    if(!writer)return fail("SD card not writable");
    const bool written=writer.write(sample,sizeof(sample))==sizeof(sample);
    writer.flush();writer.close();
    bool ok=written;
    if(ok){
        File verify=meshink_storage_open(path);
        uint8_t actual[sizeof(sample)]{};
        ok=verify&&verify.size()==sizeof(sample)&&
           verify.read(actual,sizeof(actual))==sizeof(actual)&&
           memcmp(sample,actual,sizeof(sample))==0;
        if(verify)verify.close();
    }
    const bool removed=meshink_storage_remove(path);
    if(!ok)return fail("SD write/read verification failed");
    if(!removed)return fail("SD test file could not be removed");
    return true;
}
static bool is_real_path(const char* path,uint8_t protocol,uint8_t cat){
    if(!path)return false;
    if(cat==MESHINK_BACKUP_SETTINGS&&path[0]=='@')
        return !strcmp(path,SHARED_NAME)||
               !strcmp(path,protocol==1?CORE_NVS:LEAF_NVS);
    if(path[0]!='/')return false;
    if(cat==MESHINK_BACKUP_MESSAGES)
        return !strcmp(path,protocol==1?CORE_MESSAGES:LEAF_MESSAGES);
    if(cat==MESHINK_BACKUP_NODES){
        if(protocol==2)return !strcmp(path,LEAF_NODES);
        if(!strcmp(path,"/contacts3")||!strcmp(path,"/adv_blobs"))return true;
        if(strncmp(path,"/bl/",4))return false;
        if(!path[4])return false;
        for(size_t i=4;path[i];++i){
            const char c=path[i];
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                 (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
        }
        return true;
    }
    if(cat==MESHINK_BACKUP_SETTINGS){
        if(protocol==1)
            return !strcmp(path,"/prefs.json")||!strcmp(path,"/channels2")||
                   !strcmp(path,"/identity/_main.id")||!strcmp(path,CORE_NVS);
        return !strcmp(path,LEAF_NVS);
    }
    return false;
}
static bool add_plan(size_t& count,uint8_t protocol,uint8_t cat,const char* path){
    if(!is_real_path(path,protocol,cat)||count>=MAX_ENTRIES)return false;
    for(size_t i=0;i<count;++i)if(!strcmp(plans[i].part.path,path))return true;
    Plan& p=plans[count];p=Plan{};
    p.part.category=cat;
    strncpy(p.part.path,path,sizeof(p.part.path)-1);
    if(path[0]=='@'){
        if(!strcmp(path,SHARED_NAME)){
            Preferences device;
            if(device.begin("t5-ui",true)){
                const String name=device.getString("name","");
                p.part.size=min((size_t)21,name.length()+1);
                device.end();
            }
        }else if(protocol==1){
            Preferences prefs;
            if(!prefs.begin("mesh-auth",true))return true;
            p.part.size=(uint32_t)prefs.getBytesLength("credentials");
            prefs.end();
        }else p.part.size=sizeof(LeafSettings);
    }else{
        File f=SPIFFS.open(path,"r");
        if(!f)return true; // Optional absent files are not part of the backup.
        p.part.size=f.size();
        f.close();
    }
    if(!p.part.size)return true;
    if(p.part.size>MAX_PART_BYTES)return false;
    ++count;return true;
}
static void read_leaf_settings(LeafSettings& data){
    memset(&data,0,sizeof(data));data.magic=0x31534C4DU;
    Preferences p;
    if(!p.begin("meshtastic",true))return;
    data.region=p.getUChar("region",0);
    data.preset=p.getUChar("preset",0);
    data.hop=p.getUChar("hop",3);
    data.has_private=p.getBytesLength("private")==32&&
        p.getBytes("private",data.private_key,32)==32;
    data.has_public=p.getBytesLength("public")==32&&
        p.getBytes("public",data.public_key,32)==32;
    p.end();
}
static bool load_nvs_source(const char* path,uint8_t protocol,size_t& length){
    length=0;
    if(!strcmp(path,SHARED_NAME)){
        Preferences p;
        if(!p.begin("t5-ui",true))return false;
        const String name=p.getString("name","");
        p.end();
        if(name.length()<1||name.length()>20)return false;
        length=name.length()+1;
        memcpy(nvs_data,name.c_str(),length);
        return true;
    }
    if(protocol==2){
        read_leaf_settings(leaf_data);
        memcpy(nvs_data,&leaf_data,sizeof(leaf_data));
        length=sizeof(leaf_data);
        return leaf_data.has_private&&leaf_data.has_public;
    }
    Preferences p;
    if(!p.begin("mesh-auth",true))return false;
    const size_t n=p.getBytesLength("credentials");
    bool ok=n>0&&n<=sizeof(nvs_data)&&p.getBytes("credentials",nvs_data,n)==n;
    p.end();if(ok)length=n;return ok;
}
static bool source_crc(Plan& plan,uint8_t protocol){
    uint32_t crc=0xFFFFFFFFU;size_t count=0;
    if(plan.part.path[0]=='@'){
        if(!load_nvs_source(plan.part.path,protocol,count)||count!=plan.part.size)return false;
        crc=crc32(nvs_data,count,crc);
    }else{
        File f=SPIFFS.open(plan.part.path,"r");
        if(!f)return false;
        while(count<plan.part.size){
            const size_t wanted=min(sizeof(io),(size_t)(plan.part.size-count));
            if(f.read(io,wanted)!=wanted){f.close();return false;}
            crc=crc32(io,wanted,crc);count+=wanted;
        }
        f.close();
    }
    plan.part.crc=~crc;
    return count==plan.part.size;
}
static bool source_write(File& output,const Plan& p,uint8_t protocol){
    size_t written=0;
    if(p.part.path[0]=='@'){
        size_t length=0;
        if(!load_nvs_source(p.part.path,protocol,length)||length!=p.part.size)return false;
        return output.write(nvs_data,length)==length;
    }
    File f=SPIFFS.open(p.part.path,"r");
    if(!f)return false;
    bool ok=true;
    while(written<p.part.size){
        const size_t wanted=min(sizeof(io),(size_t)(p.part.size-written));
        ok=f.read(io,wanted)==wanted&&output.write(io,wanted)==wanted;
        if(!ok)break;
        written+=wanted;
    }
    f.close();return ok&&written==p.part.size;
}
static bool allowed_filename(const char* file,uint8_t protocol){
    if(!file||strlen(file)!=12)return false;
    const char* prefix=protocol==1?"MCBK":"MTBK";
    if(strncmp(file,prefix,4))return false;
    for(int i=4;i<8;++i)if(file[i]<'0'||file[i]>'9')return false;
    return !strcmp(file+8,".BAK");
}
static bool inspect(const char* filename,uint8_t protocol,
                    Header& header,Part* entries,bool check_data){
    if(!allowed_filename(filename,protocol))return fail("Invalid backup filename");
    char path[35]="/";strncat(path,filename,sizeof(path)-2);
    File input=meshink_storage_open(path);
    if(!input)return fail("Backup file unavailable");
    if(input.read((uint8_t*)&header,sizeof(header))!=sizeof(header)||
       header.magic!=MAGIC||header.version!=FORMAT_VERSION||
       header.protocol!=protocol||header.count>MAX_ENTRIES||
       (header.categories&~7U)!=0){
        input.close();return fail("Backup format or protocol mismatch");
    }
    for(size_t i=0;i<header.count;++i){
        Part part{};
        if(input.read((uint8_t*)&part,sizeof(part))!=sizeof(part)){
            input.close();return fail("Incomplete backup manifest");
        }
        if(!memchr(part.path,0,sizeof(part.path))||
           !(part.category==1||part.category==2||part.category==4)||
           !(part.category&header.categories)||
           !is_real_path(part.path,protocol,part.category)||
           part.size==0||part.size>MAX_PART_BYTES){
            input.close();return fail("Unsafe or unsupported backup entry");
        }
        for(size_t old=0;old<i;++old)
            if(!strcmp(entries[old].path,part.path)){
                input.close();return fail("Duplicate backup entry");
            }
        entries[i]=part;
        if(check_data){
            uint32_t crc=0xFFFFFFFFU;
            size_t total=0;
            while(total<part.size){
                const size_t wanted=min(sizeof(io),(size_t)(part.size-total));
                if(input.read(io,wanted)!=wanted){
                    input.close();return fail("Truncated backup entry");
                }
                crc=crc32(io,wanted,crc);total+=wanted;
            }
            if((~crc)!=part.crc){input.close();return fail("Backup checksum mismatch");}
        }else if(!input.seek(input.position()+part.size)){
            input.close();return fail("Invalid entry size");
        }
    }
    const bool ok=input.position()==input.size();
    input.close();return ok||fail("Backup contains unexpected trailing data");
}
static bool update_core_auth_from_stage(const Stage& stage){
    File f=SPIFFS.open(stage.staged,"r");
    if(!f||f.size()>sizeof(nvs_data)){if(f)f.close();return false;}
    const size_t n=f.size();
    if(f.read(nvs_data,n)!=n){f.close();return false;}f.close();
    Preferences p;
    if(!p.begin("mesh-auth",false))return false;
    previous_nvs_size=p.getBytesLength("credentials");
    if(previous_nvs_size>sizeof(previous_nvs)){p.end();return false;}
    if(previous_nvs_size&&p.getBytes("credentials",previous_nvs,previous_nvs_size)!=previous_nvs_size){
        p.end();return false;
    }
    const bool ok=p.putBytes("credentials",nvs_data,n)==n;
    if(!ok){
        if(previous_nvs_size)p.putBytes("credentials",previous_nvs,previous_nvs_size);
        else p.remove("credentials");
    }
    p.end();return ok;
}
static bool update_leaf_from_stage(const Stage& stage){
    File f=SPIFFS.open(stage.staged,"r");
    if(!f)return false;
    LeafSettings data{};
    const bool read_ok=f.size()==sizeof(data)&&
        f.read((uint8_t*)&data,sizeof(data))==sizeof(data);
    f.close();
    if(!read_ok||data.magic!=0x31534C4DU||
       data.has_private!=1||data.has_public!=1||data.region==0||
       data.hop<1||data.hop>7)return false;
    read_leaf_settings(old_leaf_data);
    Preferences p;
    if(!p.begin("meshtastic",false))return false;
    bool ok=p.putUChar("region",data.region)==1&&
            p.putUChar("preset",data.preset)==1&&
            p.putUChar("hop",data.hop)==1&&
            p.putBytes("private",data.private_key,32)==32&&
            p.putBytes("public",data.public_key,32)==32;
    if(!ok){
        p.putUChar("region",old_leaf_data.region);
        p.putUChar("preset",old_leaf_data.preset);
        p.putUChar("hop",old_leaf_data.hop);
        if(old_leaf_data.has_private)p.putBytes("private",old_leaf_data.private_key,32);
        else p.remove("private");
        if(old_leaf_data.has_public)p.putBytes("public",old_leaf_data.public_key,32);
        else p.remove("public");
    }
    p.end();return ok;
}

static bool update_shared_name_from_stage(const Stage& stage,uint8_t protocol){
    File f=SPIFFS.open(stage.staged,"r");
    if(!f||f.size()<2||f.size()>21){if(f)f.close();return false;}
    char new_name[22]{};
    const size_t len=f.size();
    const bool ok=f.read((uint8_t*)new_name,len)==len&&
                  new_name[len-1]==0&&strlen(new_name)==len-1;
    f.close();
    if(!ok||strchr(new_name,' '))return false;
    Preferences p;
    if(!p.begin("t5-ui",false))return false;
    // Node name is deliberately shared in MeshInk. Restoring a second
    // protocol must never rename an already configured first protocol.
    const bool other_configured=protocol==1
        ?p.getBool("setup_mst",false)
        :p.getBool("setup_mc",!p.isKey("setup_mst")&&p.getBool("complete",false));
    bool saved=true;
    if(!other_configured)saved=p.putString("name",new_name)==strlen(new_name);
    p.end();
    return saved;
}

static bool snapshot_previous_nvs(Stage& stage){
    Preferences p;
    uint8_t* data=nvs_data;
    size_t size=0;
    bool exists=false;
    if(!strcmp(stage.path,CORE_NVS)){
        if(p.begin("mesh-auth",true)){
            size=p.getBytesLength("credentials");
            exists=size>0&&size<=sizeof(nvs_data)&&
                   p.getBytes("credentials",data,size)==size;
            p.end();
            if(size&&!exists)return false;
        }
    }else if(!strcmp(stage.path,LEAF_NVS)){
        read_leaf_settings(old_leaf_data);
        size=sizeof(old_leaf_data);
        memcpy(data,&old_leaf_data,size);
        exists=true;
    }else if(!strcmp(stage.path,SHARED_NAME)){
        if(p.begin("t5-ui",true)){
            const String name=p.getString("name","");
            if(name.length()){
                if(name.length()>20){p.end();return false;}
                size=name.length()+1;
                memcpy(data,name.c_str(),size);
                exists=true;
            }
            p.end();
        }
    }else return false;
    stage.old_exists=exists;
    SPIFFS.remove(stage.previous);
    if(!exists)return true;
    File backup=SPIFFS.open(stage.previous,"w");
    if(!backup)return false;
    const bool ok=backup.write(data,size)==size;
    backup.flush();backup.close();
    return ok;
}
static bool apply_leaf_snapshot(const char* filename){
    File f=SPIFFS.open(filename,"r");
    if(!f)return false;
    LeafSettings data{};
    const bool ok=f.size()==sizeof(data)&&
        f.read((uint8_t*)&data,sizeof(data))==sizeof(data);
    f.close();
    if(!ok||data.magic!=0x31534C4DU)return false;
    Preferences p;
    if(!p.begin("meshtastic",false))return false;
    bool saved=p.putUChar("region",data.region)==1 &&
        p.putUChar("preset",data.preset)==1 &&
        p.putUChar("hop",data.hop)==1;
    if(data.has_private)saved=(p.putBytes("private",data.private_key,32)==32)&&saved;
    else saved=p.remove("private")&&saved;
    if(data.has_public)saved=(p.putBytes("public",data.public_key,32)==32)&&saved;
    else saved=p.remove("public")&&saved;
    p.end();
    return saved;
}
static bool restore_previous_nvs(const Stage& stage){
    if(!stage.path[0]||stage.path[0]!='@')return true;
    if(!strcmp(stage.path,LEAF_NVS))
        return stage.old_exists ? apply_leaf_snapshot(stage.previous) : true;
    const char* name=!strcmp(stage.path,CORE_NVS)?"mesh-auth":"t5-ui";
    const char* key=!strcmp(stage.path,CORE_NVS)?"credentials":"name";
    Preferences p;
    if(!p.begin(name,false))return false;
    bool restored=false;
    if(!stage.old_exists)restored=!p.isKey(key)||p.remove(key);
    else{
        File f=SPIFFS.open(stage.previous,"r");
        if(f&&f.size()<=sizeof(nvs_data)&&f.size()){
            const size_t length=f.size();
            if(f.read(nvs_data,length)==length){
                if(!strcmp(stage.path,CORE_NVS))
                    restored=p.putBytes(key,nvs_data,length)==length;
                else if(nvs_data[length-1]==0)
                    restored=p.putString(key,(const char*)nvs_data)==length-1;
            }
        }
        if(f)f.close();
    }
    p.end();return restored;
}
static bool save_transaction(size_t count){
    SPIFFS.remove(RESTORE_TXN_TEMP);
    File f=SPIFFS.open(RESTORE_TXN_TEMP,"w");
    if(!f)return false;
    const RestoreHeader header{RESTORE_MAGIC,1,(uint16_t)count};
    const bool ok=f.write((const uint8_t*)&header,sizeof(header))==sizeof(header)&&
        f.write((const uint8_t*)stages,count*sizeof(Stage))==count*sizeof(Stage);
    f.flush();f.close();
    if(!ok){SPIFFS.remove(RESTORE_TXN_TEMP);return false;}
    if(!SPIFFS.rename(RESTORE_TXN_TEMP,RESTORE_TXN))return false;
    return true;
}
static bool rollback_transaction(size_t count){
    bool ok=true;
    // A crash can happen between rename steps. Existence of the old file,
    // rather than volatile booleans, determines what has actually changed.
    for(size_t i=count;i>0;--i){
        const Stage& stage=stages[i-1];
        if(stage.path[0]=='@')continue;
        if(SPIFFS.exists(stage.previous)){
            if(SPIFFS.exists(stage.path)&&!SPIFFS.remove(stage.path))ok=false;
            if(!SPIFFS.rename(stage.previous,stage.path))ok=false;
        }else if(!stage.old_exists &&
                 !SPIFFS.exists(stage.staged) && SPIFFS.exists(stage.path)){
            if(!SPIFFS.remove(stage.path))ok=false;
        }
    }
    for(size_t i=count;i>0;--i)
        if(stages[i-1].path[0]=='@'&&!restore_previous_nvs(stages[i-1]))ok=false;
    if(ok){
        for(size_t i=0;i<count;++i)SPIFFS.remove(stages[i].staged);
        SPIFFS.remove(RESTORE_TXN);
    }
    return ok;
}
} // namespace

bool meshink_backup_recover_pending(){
    if(!SPIFFS.exists(RESTORE_TXN))return true;
    File f=SPIFFS.open(RESTORE_TXN,"r");
    RestoreHeader head{};
    if(!f||f.read((uint8_t*)&head,sizeof(head))!=sizeof(head)||
       head.magic!=RESTORE_MAGIC||head.version!=1||
       head.count>MAX_ENTRIES||
       f.size()!=sizeof(head)+head.count*sizeof(Stage)){
        if(f)f.close();
        return fail("Restore recovery metadata invalid; storage not opened");
    }
    const size_t count=head.count;
    const bool read_ok=f.read((uint8_t*)stages,count*sizeof(Stage))==count*sizeof(Stage);
    f.close();
    if(!read_ok)return fail("Incomplete restore recovery metadata");
    // Manifest paths must remain restricted to internal paths originally
    // selected from a verified, protocol-filtered backup.
    for(size_t i=0;i<count;++i){
        Stage& stage=stages[i];
        if(!memchr(stage.path,0,sizeof(stage.path))||
           !memchr(stage.staged,0,sizeof(stage.staged))||
           !memchr(stage.previous,0,sizeof(stage.previous))||
           !(stage.category==1||stage.category==2||stage.category==4)||
           (stage.path[0]!='@'&&stage.path[0]!='/')||
           strncmp(stage.staged,"/rst",4)||
           strncmp(stage.previous,"/rst",4))
            return fail("Unsafe restore recovery entry");
    }
    if(!rollback_transaction(count))
        return fail("Interrupted restore rollback failed; refusing startup");
    Serial.println("[T5-BACKUP] interrupted restore rolled back on startup");
    return true;
}

const char* meshink_backup_error(){return last_error;}

size_t meshink_backup_list(uint8_t protocol,MeshInkBackupInfo* out,size_t capacity){
    last_error[0]=0;
    if(!out||!capacity)return 0;
    if(!sd_ready()){fail("INSERT SD CARD OR CHECK CONNECTION");return 0;}
    File root=meshink_storage_open("/");
    if(!root||!root.isDirectory())return 0;
    size_t count=0;
    File item=root.openNextFile();
    while(item){
        String path=item.name();int separator=path.lastIndexOf('/');
        const char* name=path.c_str()+separator+1;
        if(!item.isDirectory()&&allowed_filename(name,protocol)){
            Header hdr{};Part parts[MAX_ENTRIES]{};
            if(inspect(name,protocol,hdr,parts,false)&&count<capacity){
                MeshInkBackupInfo& info=out[count++];
                memset(&info,0,sizeof(info));
                strncpy(info.filename,name,sizeof(info.filename)-1);
                info.protocol=protocol;info.categories=hdr.categories;
                info.entries=hdr.count;info.bytes=item.size();
            }
        }
        item.close();item=root.openNextFile();
    }
    root.close();
    // Most recent generated backup numbers appear first.
    for(size_t i=0;i<count;++i)
        for(size_t j=i+1;j<count;++j)
            if(strcmp(out[j].filename,out[i].filename)>0){
                MeshInkBackupInfo swap=out[i];out[i]=out[j];out[j]=swap;
            }
    last_error[0]=0;return count;
}
uint8_t meshink_backup_categories(uint8_t protocol,const char* filename){
    if(!sd_ready()){fail("INSERT SD CARD OR CHECK CONNECTION");return 0;}
    Header header{};Part parts[MAX_ENTRIES]{};
    return inspect(filename,protocol,header,parts,true)?header.categories:0;
}
bool meshink_backup_create(uint8_t protocol,uint8_t categories,char* saved_path,size_t path_len){
    last_error[0]=0;
    if(saved_path&&path_len)saved_path[0]=0;
    if((protocol!=1&&protocol!=2)||!categories||(categories&~7U))
        return fail("Choose backup categories");
    if(!sd_writable())return false;
    mesh_protocol_flush_now();
    uint32_t sequence=0;size_t count=0;
    if(categories&MESHINK_BACKUP_MESSAGES){
        // Close/reopen the live journal to verify the snapshot was fully durable.
        if(!meshink_message_store().sync_and_verify_for_deep_sleep(sequence,count))
            return fail("Message journal is not durable");
    }
    size_t parts=0;
    if((categories&MESHINK_BACKUP_MESSAGES)&&
       !add_plan(parts,protocol,1,protocol==1?CORE_MESSAGES:LEAF_MESSAGES))
        return fail("Message backup preparation failed");
    if(categories&MESHINK_BACKUP_NODES){
        if(protocol==1){
            if(!add_plan(parts,protocol,2,"/contacts3")||
               !add_plan(parts,protocol,2,"/adv_blobs"))
                return fail("Contact backup preparation failed");
            File dir=SPIFFS.open("/bl");
            if(dir&&dir.isDirectory()){
                File file=dir.openNextFile();
                while(file){
                    String path=file.name();
                    if(!file.isDirectory()&&
                       !add_plan(parts,protocol,2,path.c_str())){
                        file.close();dir.close();return fail("Node blob backup too large");
                    }
                    file.close();file=dir.openNextFile();
                }
            }
            if(dir)dir.close();
        }else if(!add_plan(parts,protocol,2,LEAF_NODES))
            return fail("Node backup preparation failed");
    }
    if(categories&MESHINK_BACKUP_SETTINGS){
        if(protocol==1){
            for(const char* f:{"/prefs.json","/channels2","/identity/_main.id",CORE_NVS,SHARED_NAME})
                if(!add_plan(parts,protocol,4,f))return fail("Settings backup preparation failed");
        }else if(!add_plan(parts,protocol,4,LEAF_NVS))
            return fail("Settings backup preparation failed");
        if(!add_plan(parts,protocol,4,SHARED_NAME))return fail("Node name backup failed");
    }
    if(!parts)return fail("No selected data is available");
    for(size_t i=0;i<parts;++i)if(!source_crc(plans[i],protocol))
        return fail("Source data changed during backup");
    char name[20]{},temp[24]{},path[24]{};
    bool free_name=false;
    for(unsigned i=1;i<=9999;++i){
        snprintf(name,sizeof(name),"%s%04u.BAK",protocol==1?"MCBK":"MTBK",i);
        snprintf(path,sizeof(path),"/%s",name);
        if(!meshink_storage_exists(path)){free_name=true;break;}
    }
    if(!free_name)return fail("Backup filenames exhausted");
    snprintf(temp,sizeof(temp),"/%.8s.TMP",name);
    meshink_storage_remove(temp);
    File dest=meshink_storage_open_write(temp);
    if(!dest)return fail("Cannot create backup on SD");
    Header hdr{MAGIC,FORMAT_VERSION,protocol,categories,(uint16_t)parts,0};
    bool ok=dest.write((const uint8_t*)&hdr,sizeof(hdr))==sizeof(hdr);
    for(size_t i=0;ok&&i<parts;++i){
        const Plan& entry=plans[i];
        ok=dest.write((const uint8_t*)&entry.part,sizeof(entry.part))==sizeof(entry.part)&&
            source_write(dest,entry,protocol);
    }
    dest.flush();dest.close();
    if(!ok){meshink_storage_remove(temp);return fail("SD write failed");}
    // Validate every byte before giving the backup a permanent filename.
    // The verifier only accepts .BAK names, so inspect the temporary data
    // under the permanent filename after an atomic SD rename.
    if(!meshink_storage_rename(temp,path)){
        meshink_storage_remove(temp);return fail("Cannot finalize SD backup");
    }
    Header check{};Part items[MAX_ENTRIES]{};
    if(!inspect(name,protocol,check,items,true)){
        meshink_storage_remove(path);return false;
    }
    if(saved_path&&path_len)snprintf(saved_path,path_len,"%s",name);
    return true;
}
bool meshink_backup_restore(uint8_t protocol,const char* filename,uint8_t categories){
    last_error[0]=0;
    if((protocol!=1&&protocol!=2)||!categories||(categories&~7U))
        return fail("Choose restore categories");
    if(!sd_ready())return fail("SD card unavailable");
    Header hdr{};Part parts[MAX_ENTRIES]{};
    if(!inspect(filename,protocol,hdr,parts,true))return false;
    if((categories&hdr.categories)!=categories)return fail("Category not present in backup");
    char backup_path[36]="/";strncat(backup_path,filename,sizeof(backup_path)-2);
    File input=meshink_storage_open(backup_path);
    if(!input)return fail("Cannot reopen verified backup");
    if(!input.seek(sizeof(Header))){input.close();return fail("Cannot seek backup");}
    size_t count=0;bool ok=true;
    bool has_core_identity=false,has_core_prefs=false,has_leaf_identity=false;
    for(size_t i=0;i<hdr.count&&ok;++i){
        Part part{};
        if(input.read((uint8_t*)&part,sizeof(part))!=sizeof(part)){ok=false;break;}
        if(!(part.category&categories)){ok=input.seek(input.position()+part.size);continue;}
        if(count>=MAX_ENTRIES){ok=false;break;}
        Stage& stage=stages[count];stage=Stage{};
        strncpy(stage.path,part.path,sizeof(stage.path)-1);
        stage.category=part.category;
        snprintf(stage.staged,sizeof(stage.staged),"/rst%02u.tmp",(unsigned)count);
        snprintf(stage.previous,sizeof(stage.previous),"/rst%02u.old",(unsigned)count);
        SPIFFS.remove(stage.staged);
        File dest=SPIFFS.open(stage.staged,"w");
        if(!dest){ok=false;break;}
        uint32_t crc=0xFFFFFFFFU;size_t copied=0;
        while(copied<part.size){
            const size_t bytes=min(sizeof(io),(size_t)(part.size-copied));
            if(input.read(io,bytes)!=bytes||dest.write(io,bytes)!=bytes){ok=false;break;}
            crc=crc32(io,bytes,crc);copied+=bytes;
        }
        dest.flush();dest.close();
        if(!ok||copied!=part.size||(~crc)!=part.crc){ok=false;break;}
        if(!strcmp(part.path,"/identity/_main.id"))has_core_identity=true;
        if(!strcmp(part.path,"/prefs.json"))has_core_prefs=true;
        if(!strcmp(part.path,LEAF_NVS))has_leaf_identity=true;
        ++count;
    }
    input.close();
    if(!ok){
        for(size_t i=0;i<=count&&i<MAX_ENTRIES;++i)SPIFFS.remove(stages[i].staged);
        return fail("Restore staging failed; existing data untouched");
    }
    if((categories&MESHINK_BACKUP_SETTINGS)&&
       (protocol==1?!(has_core_identity&&has_core_prefs):!has_leaf_identity)){
        for(size_t i=0;i<count;++i)SPIFFS.remove(stages[i].staged);
        return fail("Backup missing required identity settings");
    }
    // Collect existing files/NVS snapshots and publish a durable undo manifest
    // BEFORE replacing any live protocol data. If reset interrupts this
    // section, the next boot rolls the entire operation back.
    for(size_t i=0;i<count;++i){
        Stage& stage=stages[i];
        if(stage.path[0]=='@'){
            if(!snapshot_previous_nvs(stage)){
                for(size_t j=0;j<count;++j)SPIFFS.remove(stages[j].staged);
                return fail("Cannot save existing settings for rollback");
            }
        }else{
            stage.old_exists=SPIFFS.exists(stage.path);
            if(SPIFFS.exists(stage.previous)&&!SPIFFS.remove(stage.previous))
                return fail("Cannot clear stale restore snapshot");
        }
    }
    if(!save_transaction(count))return fail("Cannot create restore recovery record");
    // Stop the running protocol and flush pending data BEFORE overwriting
    // storage. Never use the normal restart path's post-restore flush.
    mesh_protocol_flush_now();
    mesh_protocol_prepare_shutdown();
    if(categories&MESHINK_BACKUP_MESSAGES)
        meshink_message_store().prepare_for_restore();
    for(size_t i=0;i<count;++i){
        Stage& stage=stages[i];
        if(stage.path[0]=='@')continue;
        if(stage.old_exists){
            if(!SPIFFS.rename(stage.path,stage.previous)){ok=false;break;}
        }
        if(!SPIFFS.rename(stage.staged,stage.path)){ok=false;break;}
    }
    if(ok){
        for(size_t i=0;i<count;++i){
            Stage& stage=stages[i];
            if(!strcmp(stage.path,CORE_NVS))ok=update_core_auth_from_stage(stage);
            else if(!strcmp(stage.path,LEAF_NVS))ok=update_leaf_from_stage(stage);
            else if(!strcmp(stage.path,SHARED_NAME))ok=update_shared_name_from_stage(stage,protocol);
            if(!ok)break;
        }
    }
    if(!ok){
        const bool recovered=rollback_transaction(count);
        return fail(recovered?"Restore aborted; original data recovered":
                              "Restore failed and recovery is pending; restart required");
    }
    // Set the protocol's completion flag only when the complete identity
    // has been restored, allowing users to skip first-time setup on a wiped unit.
    if(categories&MESHINK_BACKUP_SETTINGS){
        Preferences ui;
        if(!ui.begin("t5-ui",false)){
            rollback_transaction(count);
            return fail("Cannot persist restored setup state");
        }
        bool saved=ui.putBool(protocol==1?"setup_mc":"setup_mst",true)==1&&
                   ui.putBool("complete",true)==1&&
                   ui.putUChar("setup_return",0)==1&&
                   ui.putUChar("setup_choice",0)==1;
        ui.end();
        if(!saved){
            rollback_transaction(count);
            return fail("Failed to persist restored setup state");
        }
    }
    // Once no further storage mutations are needed, make the result durable
    // by removing the recovery marker. Keep previous files for diagnosis.
    if(!SPIFFS.remove(RESTORE_TXN))
        return fail("Cannot finalize restore; reboot recovery required");
    for(size_t i=0;i<count;++i)SPIFFS.remove(stages[i].staged);
    return true;
}
